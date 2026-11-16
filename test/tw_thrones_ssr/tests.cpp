// The environment_specular_light replacements (src/games/twattila/shaders/ssr00-07: the same bytes in Attila and Thrones of
// Britannia, Thrones' files include Attila's) against the game's own shaders on WARP, each with g_buffers_scale 1 and 0.5,
// on a synthetic scene (a floor, a wall and a back wall in the G-buffer depth and normals, a light buffer pattern, stencil 1
// but every 16th pixel), the add-on's settings at b13 (SSR Quality 1, Effects at Output Resolution's share 1: the game's
// look), the rays' textures (t46, t47) and the hierarchical march's (t10) unbound.
// tests.exe <the game's dumped csos> <the add-on's built csos (build32\<game>.include\embed)> <ssr_quality's float in b13>
//           <effects_share's float in b13> [<an older build's csos: part F's CONTROL>]
// Shader Model 5 (66006BEA, 47D75349: ssr00 / ssr01) and 4 / 4.1 (485F33D7, 0AC579F6, 8E734BAA, C8BD2B2F: their code at
// those shader models):
// A. 100%: the replacement (nothing at b12) and the game's shader: o0 and o1.y bit for bit, o1.x (the blur's step, x
//    effects_share in ssr00 / ssr01) within 1e-6.
// B. 50%: the replacement on display-size (2x) textures holding the view in their top-left part (the rest the view's last
//    row / column, as a clamp sampler holds it at 100%), its viewport half its target, the camera's screen size the display's,
//    its viewport the view's, b12 (0.5, 0.5, 0.5, 0.5): its targets in the viewport bit for bit A's.
// C. CONTROL: the game's shader the same at 50% differs from A.
// D. CONTROL: SSR Quality 2 at 100% differs from A (the row reaches the march).
// The game's MSAA (5724C43A, 1469E169, 42F48C08, 97F6C9FC: the G-buffer, light buffer and stencil multisampled, every sample
// the scene's), at 100% only (no render scale under the game's MSAA):
// E. the replacement and the game's shader bit for bit; CONTROL SSR Quality 2 differs.
// ssr00 / ssr01 with Ray-Traced Specular Occlusion (t46 bound, noise 0.2 - 1; the environment dimmed by it):
// F. one occlusion a reflection: o1.y (the AO the reflection is multiplied by; ssr01: times its reflectance) bit for bit
//    the same as with a white AO and t46 unbound, and o0 apart from A's somewhere (t46 reaches the environment). CONTROL:
//    the older build's (else the game's) o1.y with t46 bound differs from the white-AO one (the AO on top).
// The march's share of the picture: pixels whose result changes at 100% with the light buffer black (it reflects there).
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
#include <utility>
#include <vector>

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

constexpr UINT WIDTH = 256;  // the view at 100% (powers of two: the uv's halving exact)
constexpr UINT HEIGHT = 128;
constexpr float SENTINEL = -7.f;
constexpr float NEAR_PLANE = 0.5f;
constexpr float FAR_PLANE = 1000.f;

static std::vector<char> ReadFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// A texture of `width` x `height` RGBA floats (or uints), one mip, as a shader resource.
static ComPtr<ID3D11ShaderResourceView> Texture(UINT width, UINT height, DXGI_FORMAT format, const void* data) {
  const D3D11_TEXTURE2D_DESC desc = {.Width = width, .Height = height, .MipLevels = 1, .ArraySize = 1, .Format = format,
                                     .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = data, .SysMemPitch = width * 16};
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> view;
  if (FAILED(device->CreateTexture2D(&desc, &initial, texture.GetAddressOf()))
      || FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf()))) {
    return nullptr;
  }
  return view;
}

// The view's texels (W x H) on a display-size texture (2W x 2H): the top-left part, the rest its last row / column.
template <typename T>
static std::vector<T> Display(const std::vector<T>& view) {
  std::vector<T> display(WIDTH * HEIGHT * 4 * 4);
  for (UINT y = 0; y < HEIGHT * 2; ++y) {
    for (UINT x = 0; x < WIDTH * 2; ++x) {
      const UINT from = (std::min(y, HEIGHT - 1) * WIDTH + std::min(x, WIDTH - 1)) * 4;
      std::memcpy(&display[(y * WIDTH * 2 + x) * 4], &view[from], 4 * sizeof(T));
    }
  }
  return display;
}

static float Hash(UINT x, UINT y, UINT salt) {
  UINT h = x * 73856093u ^ y * 19349663u ^ salt * 83492791u;
  h ^= h >> 13;
  h *= 0x5BD1E995u;
  h ^= h >> 15;
  return static_cast<float>(h & 0xFFFFu) / 65535.f;
}

struct Scene {
  std::vector<float> normals, channel2, channel3, depth, light;
  std::vector<uint32_t> stencil;
};

// The view: a floor (y -1.5), walls (x 4 and x -4, z 5-30), a ceiling (y 3, z 8-25) and a back wall (z 40), seen from the
// origin along +z.
static Scene MakeScene(float xs, float ys, float q) {
  Scene scene;
  for (auto* texels : {&scene.normals, &scene.channel2, &scene.channel3, &scene.depth, &scene.light}) texels->resize(WIDTH * HEIGHT * 4);
  scene.stencil.resize(WIDTH * HEIGHT * 4);
  for (UINT y = 0; y < HEIGHT; ++y) {
    for (UINT x = 0; x < WIDTH; ++x) {
      const float u = (x + 0.5f) / WIDTH;
      const float v = (y + 0.5f) / HEIGHT;
      const std::array<float, 3> ray = {(u * 2.f - 1.f) / xs, (1.f - v * 2.f) / ys, 1.f};
      float t = 40.f;
      std::array<float, 3> normal = {0.f, 0.f, -1.f};
      if (ray[1] < 0.f && -1.5f / ray[1] < t) {
        t = -1.5f / ray[1];
        normal = {0.f, 1.f, 0.f};
      }
      if (ray[0] > 0.f && 4.f / ray[0] < t && 4.f / ray[0] > 5.f && 4.f / ray[0] < 30.f) {
        t = 4.f / ray[0];
        normal = {-1.f, 0.f, 0.f};
      }
      if (ray[0] < 0.f && -4.f / ray[0] < t && -4.f / ray[0] > 5.f && -4.f / ray[0] < 30.f) {
        t = -4.f / ray[0];
        normal = {1.f, 0.f, 0.f};
      }
      if (ray[1] > 0.f && 3.f / ray[1] < t && 3.f / ray[1] > 8.f && 3.f / ray[1] < 25.f) {
        t = 3.f / ray[1];
        normal = {0.f, -1.f, 0.f};
      }
      const UINT at = (y * WIDTH + x) * 4;
      const float tilt = (Hash(x, y, 1) - 0.5f) * 0.2f;
      const float length = std::sqrt(normal[0] * normal[0] + (normal[1] + tilt) * (normal[1] + tilt) + (normal[2] + tilt) * (normal[2] + tilt));
      scene.normals[at + 0] = normal[0] / length * 0.5f + 0.5f;
      scene.normals[at + 1] = (normal[1] + tilt) / length * 0.5f + 0.5f;
      scene.normals[at + 2] = (normal[2] + tilt) / length * 0.5f + 0.5f;
      scene.normals[at + 3] = 0.6f + 0.4f * Hash(x, y, 2);
      scene.channel2[at + 3] = 0.5f + 0.5f * Hash(x, y, 3);
      scene.channel3[at + 3] = (Hash(x, y, 4) < 0.1f) ? 4.f / 255.f : 0.f;
      scene.depth[at] = q - NEAR_PLANE * q / t;
      scene.light[at + 0] = static_cast<float>(x % 32) / 32.f;
      scene.light[at + 1] = static_cast<float>(y % 16) / 16.f;
      scene.light[at + 2] = Hash(x / 8, y / 8, 5);
      scene.stencil[at + 1] = ((x + y * 7) % 16 == 0) ? 0u : 1u;
    }
  }
  return scene;
}

struct Targets {
  std::vector<float> o0, o1;
};

// One draw of `shader` (a full-screen triangle) into two RGBA float targets of `width` x `height`, its viewport the
// top-left `view_width` x `view_height`; `textures` t0-t9, b0-b2 the camera / lighting / parameters, b12 `render_scale` or none,
// b13 the add-on's settings, t46 `specular_occlusion` or none.
static Targets Draw(ID3D11PixelShader* shader, UINT width, UINT height, UINT view_width, UINT view_height,
                    const std::array<ID3D11ShaderResourceView*, 10>& textures, const std::array<ID3D11Buffer*, 3>& buffers,
                    ID3D11Buffer* render_scale, ID3D11Buffer* settings, ID3D11ShaderResourceView* specular_occlusion = nullptr) {
  static ComPtr<ID3D11VertexShader> vertex_shader;
  if (!vertex_shader) {
    constexpr char SOURCE[] = "float4 main(uint id : SV_VertexID) : SV_Position { return float4(float2((id << 1) & 2, id & 2) * float2(2, -2) + float2(-1, 1), 0, 1); }";
    ComPtr<ID3DBlob> code;
    D3DCompile(SOURCE, sizeof(SOURCE) - 1, nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, code.GetAddressOf(), nullptr);
    device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, vertex_shader.GetAddressOf());
  }
  std::array<ComPtr<ID3D11Texture2D>, 2> targets;
  std::array<ComPtr<ID3D11RenderTargetView>, 2> views;
  const D3D11_TEXTURE2D_DESC desc = {.Width = width, .Height = height, .MipLevels = 1, .ArraySize = 1,
                                     .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .SampleDesc = {.Count = 1},
                                     .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_RENDER_TARGET};
  constexpr std::array<float, 4> CLEAR = {SENTINEL, SENTINEL, SENTINEL, SENTINEL};
  for (size_t index = 0; index < 2; ++index) {
    device->CreateTexture2D(&desc, nullptr, targets[index].GetAddressOf());
    device->CreateRenderTargetView(targets[index].Get(), nullptr, views[index].GetAddressOf());
    context->ClearRenderTargetView(views[index].Get(), CLEAR.data());
  }
  ID3D11RenderTargetView* bound[] = {views[0].Get(), views[1].Get()};
  context->OMSetRenderTargets(2, bound, nullptr);
  const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(view_width), .Height = static_cast<float>(view_height), .MaxDepth = 1.f};
  context->RSSetViewports(1, &viewport);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(vertex_shader.Get(), nullptr, 0);
  context->PSSetShader(shader, nullptr, 0);
  context->PSSetShaderResources(0, 10, textures.data());
  context->PSSetShaderResources(46, 1, &specular_occlusion);
  context->PSSetConstantBuffers(0, 3, buffers.data());
  context->PSSetConstantBuffers(12, 1, &render_scale);
  context->PSSetConstantBuffers(13, 1, &settings);
  static std::array<ComPtr<ID3D11SamplerState>, 8> samplers;
  if (!samplers[0]) {
    for (UINT slot = 0; slot < 8; ++slot) {
      const bool wrap = (slot == 0 || slot == 7);  // the environment, the terrain
      const auto address = wrap ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
      const D3D11_SAMPLER_DESC sampler = {.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR, .AddressU = address, .AddressV = address,
                                          .AddressW = address, .MaxAnisotropy = 1, .ComparisonFunc = D3D11_COMPARISON_NEVER,
                                          .MaxLOD = D3D11_FLOAT32_MAX};
      device->CreateSamplerState(&sampler, samplers[slot].GetAddressOf());
    }
  }
  ID3D11SamplerState* states[8];
  for (UINT slot = 0; slot < 8; ++slot) states[slot] = samplers[slot].Get();
  context->PSSetSamplers(0, 8, states);
  context->Draw(3, 0);
  Targets out;
  for (size_t index = 0; index < 2; ++index) {
    D3D11_TEXTURE2D_DESC staging_desc = desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    device->CreateTexture2D(&staging_desc, nullptr, staging.GetAddressOf());
    context->CopyResource(staging.Get(), targets[index].Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    auto& texels = (index == 0) ? out.o0 : out.o1;
    texels.resize(static_cast<size_t>(view_width) * view_height * 4);
    for (UINT y = 0; y < view_height; ++y) {
      std::memcpy(&texels[static_cast<size_t>(y) * view_width * 4], static_cast<const char*>(mapped.pData) + y * mapped.RowPitch,
                  view_width * 16);
    }
    context->Unmap(staging.Get(), 0);
  }
  ID3D11ShaderResourceView* none[10] = {};
  context->PSSetShaderResources(0, 10, none);
  context->PSSetShaderResources(46, 1, none);
  return out;
}

// Pixels whose written components (o0 xyz, o1 xy) differ in their bits; o1.x by more than `blur_step` when given.
static UINT Differ(const Targets& a, const Targets& b, float blur_step = 0.f) {
  UINT count = 0;
  for (size_t pixel = 0; pixel < a.o0.size() / 4; ++pixel) {
    const bool o0 = std::memcmp(&a.o0[pixel * 4], &b.o0[pixel * 4], 12) != 0;
    const bool o1x = blur_step > 0.f ? std::fabs(a.o1[pixel * 4] - b.o1[pixel * 4]) > blur_step
                                     : std::memcmp(&a.o1[pixel * 4], &b.o1[pixel * 4], 4) != 0;
    const bool o1y = std::memcmp(&a.o1[pixel * 4 + 1], &b.o1[pixel * 4 + 1], 4) != 0;
    count += (o0 || o1x || o1y) ? 1u : 0u;
  }
  return count;
}

// A multisampled copy of `view` (`format`: floats or uints), every sample its texel (drawn: a multisampled texture takes no
// initial data); its samples the most of 4, 2 the format allows.
static ComPtr<ID3D11ShaderResourceView> Multisampled(ID3D11ShaderResourceView* view, DXGI_FORMAT format) {
  const bool uints = format == DXGI_FORMAT_R32G32B32A32_UINT;
  const std::string source = std::string(uints ? "Texture2D<uint4> t : register(t0); uint4" : "Texture2D<float4> t : register(t0); float4")
                             + " main(float4 p : SV_Position) : SV_Target { return t.Load(int3(p.xy, 0)); }";
  ComPtr<ID3DBlob> code;
  ComPtr<ID3D11PixelShader> copy;
  D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, code.GetAddressOf(), nullptr);
  device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, copy.GetAddressOf());
  UINT samples = 4;
  UINT levels = 0;
  while (samples > 1 && (FAILED(device->CheckMultisampleQualityLevels(format, samples, &levels)) || levels == 0)) samples /= 2;
  const D3D11_TEXTURE2D_DESC desc = {.Width = WIDTH, .Height = HEIGHT, .MipLevels = 1, .ArraySize = 1, .Format = format,
                                     .SampleDesc = {.Count = samples}, .Usage = D3D11_USAGE_DEFAULT,
                                     .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET};
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11RenderTargetView> target;
  ComPtr<ID3D11ShaderResourceView> result;
  device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf());
  device->CreateRenderTargetView(texture.Get(), nullptr, target.GetAddressOf());
  device->CreateShaderResourceView(texture.Get(), nullptr, result.GetAddressOf());
  static ComPtr<ID3D11VertexShader> triangle;
  if (!triangle) {
    constexpr char SOURCE[] = "float4 main(uint id : SV_VertexID) : SV_Position { return float4(float2((id << 1) & 2, id & 2) * float2(2, -2) + float2(-1, 1), 0, 1); }";
    ComPtr<ID3DBlob> vertex_code;
    D3DCompile(SOURCE, sizeof(SOURCE) - 1, nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, vertex_code.GetAddressOf(), nullptr);
    device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, triangle.GetAddressOf());
  }
  ID3D11RenderTargetView* bound = target.Get();
  context->OMSetRenderTargets(1, &bound, nullptr);
  const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(WIDTH), .Height = static_cast<float>(HEIGHT), .MaxDepth = 1.f};
  context->RSSetViewports(1, &viewport);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(triangle.Get(), nullptr, 0);
  context->PSSetShader(copy.Get(), nullptr, 0);
  context->PSSetShaderResources(0, 1, &view);
  context->Draw(3, 0);
  ID3D11ShaderResourceView* none = nullptr;
  context->PSSetShaderResources(0, 1, &none);
  context->OMSetRenderTargets(0, nullptr, nullptr);
  return result;
}

static ComPtr<ID3D11Buffer> Constants(const void* data, UINT bytes) {
  const D3D11_BUFFER_DESC desc = {.ByteWidth = bytes, .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = data};
  ComPtr<ID3D11Buffer> buffer;
  device->CreateBuffer(&desc, &initial, buffer.GetAddressOf());
  return buffer;
}

// A matrix as the game's camera holds it (row vectors: register c of a 4x4 its column c).
static void PutMatrix(std::array<float, 144>* camera, UINT first, const double (&matrix)[4][4]) {
  for (UINT column = 0; column < 4; ++column) {
    for (UINT row = 0; row < 4; ++row) (*camera)[(first + column) * 4 + row] = static_cast<float>(matrix[row][column]);
  }
}

static void Invert(const double (&m)[4][4], double (&out)[4][4]) {
  double a[4][8];
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 8; ++c) a[r][c] = (c < 4) ? m[r][c] : (c - 4 == r ? 1.0 : 0.0);
  }
  for (int c = 0; c < 4; ++c) {
    int pivot = c;
    for (int r = c + 1; r < 4; ++r) {
      if (std::fabs(a[r][c]) > std::fabs(a[pivot][c])) pivot = r;
    }
    for (int k = 0; k < 8; ++k) std::swap(a[c][k], a[pivot][k]);
    const double scale = a[c][c];
    for (int k = 0; k < 8; ++k) a[c][k] /= scale;
    for (int r = 0; r < 4; ++r) {
      if (r == c) continue;
      const double factor = a[r][c];
      for (int k = 0; k < 8; ++k) a[r][k] -= factor * a[c][k];
    }
  }
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) out[r][c] = a[r][c + 4];
  }
}

int main(int argc, char** argv) {
  if (argc < 5) {
    std::printf("usage: tests.exe <the game's dumped csos> <the add-on's built csos> <ssr_quality's float in b13> <effects_share's>"
                " [<an older build's csos>]\n");
    return 2;
  }
  const std::string game_folder = argv[1];
  const std::string built_folder = argv[2];
  const int ssr_quality = std::atoi(argv[3]);
  const int effects_share = std::atoi(argv[4]);
  const std::string older_folder = (argc > 5) ? argv[5] : "";
  const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                               context.GetAddressOf()))) {
    std::printf("FAIL: no WARP device\n");
    return 1;
  }

  // The camera: at the origin, the view's projection (60 degrees, row vectors: clip = view x projection).
  const double ys = 1.0 / std::tan(30.0 * 3.14159265358979323846 / 180.0);
  const double xs = ys * HEIGHT / WIDTH;
  const double q = FAR_PLANE / (FAR_PLANE - NEAR_PLANE);
  const double identity[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
  const double projection[4][4] = {{xs, 0, 0, 0}, {0, ys, 0, 0}, {0, 0, q, 1}, {0, 0, -NEAR_PLANE * q, 0}};
  double inverse[4][4];
  Invert(projection, inverse);
  const auto camera_for = [&](UINT screen_width, UINT screen_height) {
    std::array<float, 144> camera = {};
    PutMatrix(&camera, 1, identity);
    PutMatrix(&camera, 5, projection);
    PutMatrix(&camera, 9, projection);
    PutMatrix(&camera, 13, identity);
    PutMatrix(&camera, 17, inverse);
    PutMatrix(&camera, 21, inverse);
    camera[25 * 4 + 0] = NEAR_PLANE;
    camera[25 * 4 + 1] = FAR_PLANE;
    const std::array<float, 4> screen = {static_cast<float>(screen_width), static_cast<float>(screen_height), 1.f / screen_width,
                                         1.f / screen_height};
    const std::array<float, 4> view = {static_cast<float>(WIDTH), static_cast<float>(HEIGHT), 1.f / WIDTH, 1.f / HEIGHT};
    std::memcpy(&camera[27 * 4], screen.data(), 16);
    std::memcpy(&camera[29 * 4], view.data(), 16);
    std::memcpy(&camera[30 * 4], screen.data(), 16);
    return Constants(camera.data(), sizeof(camera));
  };
  // lighting_VS_PS: g_apply_environment_specular 1 (c0.x, unused), sun_direction (c0.yzw; .y, c0.z, is the one read),
  // sun_colour, g_hdr_on.
  std::array<float, 48> lighting = {};
  const uint32_t yes = 1;
  std::memcpy(&lighting[0], &yes, 4);
  lighting[1] = 0.3f;
  lighting[2] = -0.8f;
  lighting[3] = 0.52f;
  lighting[4] = 1.f;
  lighting[5] = 0.9f;
  lighting[6] = 0.8f;
  lighting[44] = 1.f;
  const auto lighting_buffer = Constants(lighting.data(), sizeof(lighting));
  const auto parameters_for = [&](float buffers_scale) {
    std::array<float, 24> parameters = {};
    // g_flags 0, ray length x 1, 24 samples; acceptance 0.05-1; border falloff 0.7-0.95; depth bias 0.01; depth falloff 0-1.5;
    // blur radius (1, 1), steep adjustment 0.001; terrain offset / scale (0, 0, 100, 100); g_buffers_scale.
    const std::array<float, 21> values = {0.f, 1.f, 24.f, 0.f, 0.05f, 1.f, 0.7f, 0.95f, 0.01f, 0.f, 1.5f, 0.f,
                                          1.f, 1.f, 0.001f, 0.f, 0.f, 0.f, 100.f, 100.f, buffers_scale};
    std::memcpy(parameters.data(), values.data(), sizeof(values));
    return Constants(parameters.data(), sizeof(parameters));
  };
  const std::array<float, 4> half = {0.5f, 0.5f, 0.5f, 0.5f};
  const auto render_scale = Constants(half.data(), 16);

  const Scene scene = MakeScene(static_cast<float>(xs), static_cast<float>(ys), static_cast<float>(q));
  const std::vector<float> black(WIDTH * HEIGHT * 4, 0.f);
  std::vector<float> ssao(WIDTH / 2 * HEIGHT / 2 * 4), jitter(64 * 64 * 4), terrain(64 * 64 * 4);
  for (UINT i = 0; i < ssao.size(); ++i) ssao[i] = Hash(i, 7, 6);
  for (UINT i = 0; i < jitter.size(); ++i) jitter[i] = Hash(i, 3, 7);
  for (UINT i = 0; i < terrain.size(); ++i) terrain[i] = Hash(i, 5, 8);
  // The environment cube: a colour per face.
  std::vector<uint32_t> faces(16 * 16 * 6);
  for (UINT i = 0; i < faces.size(); ++i) faces[i] = 0xFF000000u | (0x3050A0u * (i / 256 + 1));
  D3D11_SUBRESOURCE_DATA face_data[6];
  for (UINT face = 0; face < 6; ++face) face_data[face] = {.pSysMem = &faces[face * 256], .SysMemPitch = 64};
  const D3D11_TEXTURE2D_DESC cube_desc = {.Width = 16, .Height = 16, .MipLevels = 1, .ArraySize = 6, .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                                          .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_SHADER_RESOURCE,
                                          .MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE};
  ComPtr<ID3D11Texture2D> cube;
  ComPtr<ID3D11ShaderResourceView> environment;
  device->CreateTexture2D(&cube_desc, face_data, cube.GetAddressOf());
  device->CreateShaderResourceView(cube.Get(), nullptr, environment.GetAddressOf());
  const auto shared_ssao = Texture(WIDTH / 2, HEIGHT / 2, DXGI_FORMAT_R32G32B32A32_FLOAT, ssao.data());
  const std::vector<float> ones(WIDTH / 2 * HEIGHT / 2 * 4, 1.f);
  const auto white_ssao = Texture(WIDTH / 2, HEIGHT / 2, DXGI_FORMAT_R32G32B32A32_FLOAT, ones.data());
  // Part F's specular occlusion at the render pixels (x read).
  std::vector<float> open(WIDTH * HEIGHT * 4);
  for (UINT i = 0; i < open.size(); ++i) open[i] = 0.2f + 0.8f * Hash(i / 4, 11, 9);
  const auto specular_occlusion = Texture(WIDTH, HEIGHT, DXGI_FORMAT_R32G32B32A32_FLOAT, open.data());
  const auto shared_jitter = Texture(64, 64, DXGI_FORMAT_R32G32B32A32_FLOAT, jitter.data());
  const auto shared_terrain = Texture(64, 64, DXGI_FORMAT_R32G32B32A32_FLOAT, terrain.data());
  constexpr auto FLOATS = DXGI_FORMAT_R32G32B32A32_FLOAT;
  constexpr auto UINTS = DXGI_FORMAT_R32G32B32A32_UINT;
  // The view's textures at 100% and on display-size ones (50%), the light buffer also black.
  const auto view_textures = [&](bool display, const std::vector<float>& light) {
    const UINT w = display ? WIDTH * 2 : WIDTH;
    const UINT h = display ? HEIGHT * 2 : HEIGHT;
    const auto put = [&](const std::vector<float>& texels) { return Texture(w, h, FLOATS, display ? Display(texels).data() : texels.data()); };
    return std::array<ComPtr<ID3D11ShaderResourceView>, 6>{
        put(scene.normals), put(scene.channel2), put(scene.channel3), put(scene.depth), put(light),
        Texture(w, h, UINTS, display ? Display(scene.stencil).data() : scene.stencil.data())};
  };
  const auto at_100 = view_textures(false, scene.light);
  const auto at_100_black = view_textures(false, black);
  const auto at_50 = view_textures(true, scene.light);
  const auto bindings = [&](const std::array<ComPtr<ID3D11ShaderResourceView>, 6>& view) {
    return std::array<ID3D11ShaderResourceView*, 10>{view[0].Get(), view[1].Get(), view[2].Get(), view[3].Get(), environment.Get(),
                                                     shared_terrain.Get(), view[4].Get(), shared_ssao.Get(), view[5].Get(), shared_jitter.Get()};
  };
  const auto camera_100 = camera_for(WIDTH, HEIGHT);
  const auto camera_50 = camera_for(WIDTH * 2, HEIGHT * 2);

  // The add-on's settings (b13): SSR Quality 1, Effects at Output Resolution's share 1 (the game's look), or SSR Quality 2.
  const auto settings_for = [&](float quality) {
    std::vector<float> values(256 * 4, 0.f);
    values[ssr_quality] = quality;
    values[effects_share] = 1.f;
    return Constants(values.data(), static_cast<UINT>(values.size() * 4));
  };
  const auto settings = settings_for(1.f);
  const auto settings_2 = settings_for(2.f);
  // ssr00 / ssr01's blur step (o1.x) through effects_share 1: a few ulp off the game's.
  constexpr float BLUR_STEP = 1e-6f;

  struct Pair {
    const char* name;
    const char* game;
    const char* replacement;
    bool multisampled;
    bool occlusion;  // reads Ray-Traced Specular Occlusion (t46): part F
  };
  constexpr std::array<Pair, 10> PAIRS = {{
      {"ssr00 sm_5_0 standard", "0x66006BEA.ps_5_0.cso", "0x66006BEA.cso", false, true},
      {"ssr01 sm_5_0 full_res", "0x47D75349.ps_5_0.cso", "0x47D75349.cso", false, true},
      {"ssr02 sm_4_1 standard", "0x485F33D7.ps_4_1.cso", "0x485F33D7.cso", false, false},
      {"ssr03 sm_4_1 full_res", "0x0AC579F6.ps_4_1.cso", "0x0AC579F6.cso", false, false},
      {"ssr04 sm_4_0 standard", "0x8E734BAA.ps_4_0.cso", "0x8E734BAA.cso", false, false},
      {"ssr05 sm_4_0 full_res", "0xC8BD2B2F.ps_4_0.cso", "0xC8BD2B2F.cso", false, false},
      {"ssr06 sm_5_0 standard MSAA", "0x5724C43A.ps_5_0.cso", "0x5724C43A.cso", true, false},
      {"ssr06 sm_4_1 standard MSAA", "0x1469E169.ps_4_1.cso", "0x1469E169.cso", true, false},
      {"ssr07 sm_5_0 full_res MSAA", "0x42F48C08.ps_5_0.cso", "0x42F48C08.cso", true, false},
      {"ssr07 sm_4_1 full_res MSAA", "0x97F6C9FC.ps_4_1.cso", "0x97F6C9FC.cso", true, false},
  }};
  // The game's MSAA: the view's G-buffer, light buffer and stencil multisampled (t3-t8 of its builds).
  const auto multisampled = [&](const std::array<ComPtr<ID3D11ShaderResourceView>, 6>& view) {
    std::array<ComPtr<ID3D11ShaderResourceView>, 6> copies;
    for (size_t index = 0; index < 6; ++index) copies[index] = Multisampled(view[index].Get(), index == 5 ? UINTS : FLOATS);
    return copies;
  };
  const auto ms_100 = multisampled(at_100);
  const auto ms_100_black = multisampled(at_100_black);
  const auto ms_bindings = [&](const std::array<ComPtr<ID3D11ShaderResourceView>, 6>& view) {
    return std::array<ID3D11ShaderResourceView*, 10>{environment.Get(), shared_terrain.Get(), shared_ssao.Get(), view[0].Get(), view[1].Get(),
                                                     view[2].Get(), view[3].Get(), view[4].Get(), view[5].Get(), shared_jitter.Get()};
  };
  for (const Pair& pair : PAIRS) {
    const auto game_code = ReadFile(game_folder + "\\" + pair.game);
    const auto replacement_code = ReadFile(built_folder + "\\" + pair.replacement);
    ComPtr<ID3D11PixelShader> game;
    ComPtr<ID3D11PixelShader> replacement;
    if (game_code.empty() || replacement_code.empty()
        || FAILED(device->CreatePixelShader(game_code.data(), game_code.size(), nullptr, game.GetAddressOf()))
        || FAILED(device->CreatePixelShader(replacement_code.data(), replacement_code.size(), nullptr, replacement.GetAddressOf()))) {
      std::printf("FAIL %s: shaders not loaded (%s, %s)\n", pair.name, pair.game, pair.replacement);
      ++failures;
      continue;
    }
    for (const float buffers_scale : {1.f, 0.5f}) {
      const auto parameters = parameters_for(buffers_scale);
      const std::array<ID3D11Buffer*, 3> buffers_100 = {camera_100.Get(), lighting_buffer.Get(), parameters.Get()};
      const std::array<ID3D11Buffer*, 3> buffers_50 = {camera_50.Get(), lighting_buffer.Get(), parameters.Get()};
      const auto view_w = static_cast<UINT>(WIDTH * buffers_scale);
      const auto view_h = static_cast<UINT>(HEIGHT * buffers_scale);
      const auto scene_100 = pair.multisampled ? ms_bindings(ms_100) : bindings(at_100);
      const auto scene_black = pair.multisampled ? ms_bindings(ms_100_black) : bindings(at_100_black);
      const Targets game_100 = Draw(game.Get(), view_w, view_h, view_w, view_h, scene_100, buffers_100, nullptr, settings.Get());
      const Targets ours_100 = Draw(replacement.Get(), view_w, view_h, view_w, view_h, scene_100, buffers_100, nullptr, settings.Get());
      const Targets ours_quality_2 = Draw(replacement.Get(), view_w, view_h, view_w, view_h, scene_100, buffers_100, nullptr, settings_2.Get());
      const Targets game_black = Draw(game.Get(), view_w, view_h, view_w, view_h, scene_black, buffers_100, nullptr, settings.Get());
      UINT written = 0;
      for (size_t pixel = 0; pixel < game_100.o0.size() / 4; ++pixel) written += (game_100.o1[pixel * 4] != SENTINEL) ? 1u : 0u;
      const UINT reflecting = Differ(game_100, game_black);
      const UINT a = Differ(game_100, ours_100, pair.multisampled ? 0.f : BLUR_STEP);
      const UINT d = Differ(ours_100, ours_quality_2);
      CHECK(written > view_w * view_h / 2 && reflecting > written / 20, "%s: the scene exercises too little", pair.name);
      CHECK(a == 0, "%s: 100%% not the game's", pair.name);
      CHECK(d > 0, "%s: control D (SSR Quality 2) does not fail", pair.name);
      if (pair.multisampled) {
        std::printf("%s, g_buffers_scale %.1f: %u x %u pixels, %u drawn, %u reflect the light buffer; E 100%%: %u differ; CONTROL SSR "
                    "Quality 2: %u differ\n", pair.name, buffers_scale, view_w, view_h, written, reflecting, a, d);
        continue;
      }
      const Targets ours_50 = Draw(replacement.Get(), view_w * 2, view_h * 2, view_w, view_h, bindings(at_50), buffers_50, render_scale.Get(), settings.Get());
      const Targets game_50 = Draw(game.Get(), view_w * 2, view_h * 2, view_w, view_h, bindings(at_50), buffers_50, render_scale.Get(), settings.Get());
      const UINT b = Differ(ours_100, ours_50);
      const UINT c = Differ(game_100, game_50);
      std::printf("%s, g_buffers_scale %.1f: %u x %u pixels, %u drawn, %u reflect the light buffer; A 100%%: %u differ; B 50%%: %u "
                  "differ; CONTROL C the game's at 50%%: %u differ; CONTROL D SSR Quality 2: %u differ\n",
                  pair.name, buffers_scale, view_w, view_h, written, reflecting, a, b, c, d);
      CHECK(b == 0, "%s: 50%% not 100%%'s", pair.name);
      CHECK(c > 0, "%s: control C does not fail", pair.name);
      if (!pair.occlusion) continue;
      // F: the AO's reflections under the specular occlusion.
      ComPtr<ID3D11PixelShader> older;
      if (const auto older_code = older_folder.empty() ? std::vector<char>() : ReadFile(older_folder + "\\" + pair.replacement);
          !older_code.empty()) {
        device->CreatePixelShader(older_code.data(), older_code.size(), nullptr, older.GetAddressOf());
      }
      auto scene_white = scene_100;
      scene_white[7] = white_ssao.Get();
      const Targets ours_occluded = Draw(replacement.Get(), view_w, view_h, view_w, view_h, scene_100, buffers_100, nullptr, settings.Get(),
                                         specular_occlusion.Get());
      const Targets ours_white = Draw(replacement.Get(), view_w, view_h, view_w, view_h, scene_white, buffers_100, nullptr, settings.Get());
      const Targets control_occluded = Draw(older ? older.Get() : game.Get(), view_w, view_h, view_w, view_h, scene_100, buffers_100, nullptr,
                                            settings.Get(), specular_occlusion.Get());
      const auto ao_differ = [](const Targets& x, const Targets& y) {
        UINT count = 0;
        for (size_t pixel = 0; pixel < x.o1.size() / 4; ++pixel) count += (std::memcmp(&x.o1[pixel * 4 + 1], &y.o1[pixel * 4 + 1], 4) != 0) ? 1u : 0u;
        return count;
      };
      const auto colour_differ = [](const Targets& x, const Targets& y) {
        UINT count = 0;
        for (size_t pixel = 0; pixel < x.o0.size() / 4; ++pixel) count += (std::memcmp(&x.o0[pixel * 4], &y.o0[pixel * 4], 12) != 0) ? 1u : 0u;
        return count;
      };
      const UINT f = ao_differ(ours_occluded, ours_white);
      const UINT dimmed = colour_differ(ours_occluded, ours_100);
      const UINT f_control = ao_differ(control_occluded, ours_white);
      std::printf("%s, g_buffers_scale %.1f: F specular occlusion: o1.y %u differ from the white AO's, o0 %u dimmed; CONTROL %s with "
                  "it: o1.y %u differ\n", pair.name, buffers_scale, f, dimmed, older ? "the older build" : "the game's", f_control);
      CHECK(f == 0, "%s: the AO still multiplies the reflection under the specular occlusion", pair.name);
      CHECK(dimmed > 0, "%s: the specular occlusion does not reach the environment", pair.name);
      CHECK(f_control > 0, "%s: control F does not fail", pair.name);
    }
  }
  std::printf("%s\n", failures == 0 ? "PASS" : "FAIL");
  return failures == 0 ? 0 : 1;
}
