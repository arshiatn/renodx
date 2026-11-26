// The screen-space shadows' extended march (twattila/screen_space_shadows.hpp, screen_space_lights.hpp and their shaders)
// on WARP, through the module's own Compute, OnThinDraw and OnLightDraw with Attila's embedded shaders (build32\
// twattila.include). tests.exe <fc24dbce's plain shaders, run.ps1's before_cso> [--time | --time-only]
// The scenes: a ground plane 2 m below a camera looking 25 degrees down (60 degrees high), boxes on it, ray-traced on the
// CPU into the depth (and the G-buffer's colour, the banners' depth) and a reference shadow.
// 1. Rows off: the occlusion and the softened occlusion to the bit as fc24dbce's sss_depth / sss_setup / sss_bend / sss_blur
//    give them (sss_setup's dispatch list moved into sss_dispatch.hlsli; its arguments and parameters to the bit too), and
//    the extended march with nothing to do (no banners in view, Auto Thickness off) the plain march's to the bit, with
//    foliage marked (by the depth copies, Warhammer III's material byte, and Pharaoh's bytes with bit 128; CONTROL: its
//    water's bit 64 must differ). Pixels Bend's waves write twice in one dispatch vary between runs of the same shaders on WARP (both
//    sides', under 0.2% of the picture): 8 runs each in turns, a pixel apart where no value of one side's is the other's.
//    At 50%, 58% and 77% render scale (30, 35 and 46 samples, fade-outs 14, 19 and 30 after 16 hard ones) both marches
//    take the counts in fours (X27): the plain march's with 32, 36 and 48, 12, 16 and 28, to the bit. CONTROL (must
//    differ): fc24dbce's plain march with the odd counts.
// 2. Auto Thickness: a deep box and a thick wall the sun shines past towards the camera leak less light behind their
//    fronts (fewer ground pixels wrong against the reference), a pole no worse (5% + 20 pixels).
// 3. A red banner: its shadow lets Light Through Banners of the sun through (its occlusion its solid one's x 0.65, within
//    0.12), its filter is the banner's colour over its brightest channel (Coloured Shadows: green and blue taken, red
//    kept), none in the sun. Pharaoh's form (the sun at float 4, an sRGB colour, power 1): the same within 0.01; CONTROL: a
//    decoy direction at Attila's float 1 read there must differ.
// 4. Softer Away from the Caster: the penumbra each shadowed pixel gets is its caster's distance x tan(half the disk) in
//    render pixels (median within 25% of the reference caster's), growing away from a pole; the softened shadow's edge
//    widens with it. Softness 0 and the penumbra's texture not made: the sun draws read the occlusion (no pass softened it).
// 5. Torch and Fire Shadows: a light beside a box: its bit agrees with a ray-traced reference on 95% of the ground within
//    its reach, all bits set outside the rectangle its reach covers; of 100 lights (one repeated) the pick is the CPU's
//    partial_sort by the rays' score, in order, the repeat once. Pharaoh's 64-byte instances: the same light and bits.
// 6. Light Through Banners' replay: a banner's G-buffer draw drawn again after it: tagged (its depth the scene's) where it
//    stays in view, not where a later draw covers it, far elsewhere; the depth, targets (one kept at slot 6), shader,
//    rasterizer, depth and blend states as they were.
// 7. Rows turned off (sss::Update): Coloured Shadows' filter (taken off t50 at the next sun draw), the penumbra, the
//    lights' objects, the banners' depth let go (their resources' last reference the test's), made again when turned on.
// 8. Shogun 2's own sun shadow passes (twshogun2completeedition/shaders/sss00 / sss01 / sss02, whose bytes changed since
//    fc24dbce: Bend's parameters as rows, Light Through Foliage's caster light) at the rows' defaults (shader_injection all
//    0: the game's parameters, foliage off) to the bit as fc24dbce's (run.ps1's csos) on the box scene: its depth, the sun's
//    Bend dispatches as sss_setup lists them, 4 cascades of random depths; 8 runs each in turns, as case 1. CONTROLS (must
//    differ): Bend's contrast row at 2 (sss00, sss01), the shadow map stability row at 2 (sss02).
// 9. Torch and Fire Shadows at a battle's light count (5120 instances in 128 draws, repeats and mirrored lights): every
//    output as f6408aa1's sss_lights gives it (run.ps1's sss_lights_f6408aa1.cso), the pick the CPU's. CONTROL: the CPU's
//    pick without the repeat test must differ. A torch on the camera's plane (clip w 0): its coordinate finite (X29, Bend's
//    w == 0 guard); CONTROL (must fail): f6408aa1's gives an infinite depth.
// 10. Shogun 2's Light Through Banners and Coloured Shadows through its extended passes (sss_distance / _cascades from this
//     tree) on case 3's banner: its shadow keeps the row's share of the sun, its filter is its colour, the cascades' version
//     the same; rows off the game's sss01 to the bit. CONTROLS (must fail): the banners' depth or the filter's target unbound.
// 11. Screen steps (O-SS5, plan_1117 designs\sss_scale_thickness.md W1-W8, W10): below 100% render scale the march
//     one sample a screen pixel between the render texels against the 100% march (A/A: half a render pixel's jitter);
//     each with a CONTROL that must fail (today's form C, render steps, or the module's shader with a test define), but
//     W6 (near the light: its controls numbers only, their difference under a render pixel by geometry).
// 12. Same surface near the far plane (X30): Softness's sss_blur keeps every tap on flat ground at 40-55% of a 16 m far
//     plane (its distance from the depth at infinity, A, sss_setup's); CONTROL (must fail): bc423ef9d's (1 / (1 - depth))
//     rejects some.
// --time: on the hardware GPU at 3840 x 2160, each row's GPU time (median of 30 frames), sss_bend at 100% to 33% render
// scale (O-SS5's A' against C, gate G1), torches near and far, the CPU time of a later light draw, and a battle's light
// draws with today's sss_lights and f6408aa1's. --time-only: that alone.
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <numeric>
#include <random>
#include <ranges>
#include <span>
#include <string>
#include <vector>
#include "src/games/twattila/screen_space_lights.hpp"
#include "test/tw_particle_history/mock_reshade.hpp"
// The sites' pass_timing.hpp includes utils/settings.hpp (the add-on's NAME).
extern "C" __declspec(dllexport) const char* const NAME = "Screen-space shadows test";

// Shogun 2's shader_injection (case 8: a row's place in it).
namespace shogun2 {
#include "src/games/twshogun2completeedition/shared.h"
}  // namespace shogun2

using Microsoft::WRL::ComPtr;
using Vector = std::array<float, 3>;
using Matrix = std::array<std::array<float, 4>, 4>;  // m[row][column], row vectors: clip = position x m
namespace action = renodx::utils::command_action;
namespace sss = twattila::screen_space_shadows;
namespace lights = twattila::screen_space_lights;
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

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;

static void MakeDevice(D3D_DRIVER_TYPE type) {
  sss::internal::runtime.objects = {};
  device.Reset();
  context.Reset();
  REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, type, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                                      context.GetAddressOf())));
}

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

static std::vector<uint8_t> ReadFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// ---- Geometry ---------------------------------------------------------------------------------------------------------

constexpr float NEAR_PLANE = 0.1f;
constexpr float FAR_PLANE = 2000.f;
constexpr float GROUND_Y = -2.f;
constexpr float TAN_HALF_Y = 0.57735027f;  // 60 degrees high

static Vector Normalize(Vector v) {
  const float length = std::sqrt((v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]));
  return {v[0] / length, v[1] / length, v[2] / length};
}
static float Distance(const Vector& a, const Vector& b) {
  return std::sqrt(((a[0] - b[0]) * (a[0] - b[0])) + ((a[1] - b[1]) * (a[1] - b[1])) + ((a[2] - b[2]) * (a[2] - b[2])));
}

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
  Matrix inverse = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
  for (int column = 0; column < 4; ++column) {
    int pivot = column;
    for (int row = column + 1; row < 4; ++row) {
      if (std::fabs(m[row][column]) > std::fabs(m[pivot][column])) pivot = row;
    }
    std::swap(m[column], m[pivot]);
    std::swap(inverse[column], inverse[pivot]);
    const float scale = 1.f / m[column][column];
    for (int j = 0; j < 4; ++j) {
      m[column][j] *= scale;
      inverse[column][j] *= scale;
    }
    for (int row = 0; row < 4; ++row) {
      if (row == column) continue;
      const float factor = m[row][column];
      for (int j = 0; j < 4; ++j) {
        m[row][j] -= factor * m[column][j];
        inverse[row][j] -= factor * inverse[column][j];
      }
    }
  }
  return inverse;
}

enum class Kind { SOLID, THIN, FOLIAGE };

struct Box {
  Vector low;
  Vector high;
  Kind kind = Kind::SOLID;
  Vector albedo = {0.5f, 0.5f, 0.5f};
};

// The ray from `origin` along `direction` into `box`: its entry distance (> 0) or none.
static bool HitBox(const Box& box, const Vector& origin, const Vector& direction, float* entry) {
  float t0 = 0.f;
  float t1 = 1e30f;
  for (int axis = 0; axis < 3; ++axis) {
    if (std::fabs(direction[axis]) < 1e-9f) {
      if (origin[axis] < box.low[axis] || origin[axis] > box.high[axis]) return false;
      continue;
    }
    float ta = (box.low[axis] - origin[axis]) / direction[axis];
    float tb = (box.high[axis] - origin[axis]) / direction[axis];
    if (ta > tb) std::swap(ta, tb);
    t0 = std::max(t0, ta);
    t1 = std::min(t1, tb);
    if (t0 > t1) return false;
  }
  *entry = t0;
  return true;
}

struct Camera {
  UINT width = 0;
  UINT height = 0;
  Matrix view = {};
  Matrix projection = {};
  Matrix view_projection = {};
  float jitter = 0.f;  // Trace's rays this many pixels right and down of the pixels' centres (case 11's A/A noise)
};

static Camera MakeCamera(UINT width, UINT height, float tilt_degrees = 25.f, float far_plane = FAR_PLANE) {
  const float tilt = tilt_degrees * 3.14159265f / 180.f;
  const float c = std::cos(tilt);
  const float s = std::sin(tilt);
  const float tan_half_x = TAN_HALF_Y * static_cast<float>(width) / static_cast<float>(height);
  Camera camera = {.width = width, .height = height};
  camera.view = {{{1, 0, 0, 0}, {0, c, -s, 0}, {0, s, c, 0}, {0, 0, 0, 1}}};
  camera.projection = {{{1.f / tan_half_x, 0, 0, 0},
                        {0, 1.f / TAN_HALF_Y, 0, 0},
                        {0, 0, far_plane / (far_plane - NEAR_PLANE), 1},
                        {0, 0, -NEAR_PLANE * far_plane / (far_plane - NEAR_PLANE), 0}}};
  camera.view_projection = Multiply(camera.view, camera.projection);
  return camera;
}

static std::array<float, 4> Project(const Camera& camera, const Vector& p) {
  std::array<float, 4> clip = {};
  for (int j = 0; j < 4; ++j) {
    clip[j] = (p[0] * camera.view_projection[0][j]) + (p[1] * camera.view_projection[1][j]) + (p[2] * camera.view_projection[2][j])
              + camera.view_projection[3][j];
  }
  return clip;
}

// What each pixel sees: its depth (1 the sky), the box it is of (-1 the ground, -2 the sky), its world position.
struct Picture {
  std::vector<float> depth;
  std::vector<int> hit;
  std::vector<Vector> position;
};

static Picture Trace(const Camera& camera, std::span<const Box> boxes) {
  const UINT w = camera.width;
  const UINT h = camera.height;
  const float tan_half_x = TAN_HALF_Y * static_cast<float>(w) / static_cast<float>(h);
  Picture picture = {.depth = std::vector<float>(w * h, 1.f), .hit = std::vector<int>(w * h, -2), .position = std::vector<Vector>(w * h)};
  for (UINT y = 0; y < h; ++y) {
    for (UINT x = 0; x < w; ++x) {
      const float ndc_x = ((static_cast<float>(x) + 0.5f + camera.jitter) / static_cast<float>(w) * 2.f) - 1.f;
      const float ndc_y = 1.f - ((static_cast<float>(y) + 0.5f + camera.jitter) / static_cast<float>(h) * 2.f);
      const Vector view_direction = {ndc_x * tan_half_x, ndc_y * TAN_HALF_Y, 1.f};
      Vector direction = {};
      for (int j = 0; j < 3; ++j) {
        direction[j] = (view_direction[0] * camera.view[j][0]) + (view_direction[1] * camera.view[j][1]) + (view_direction[2] * camera.view[j][2]);
      }
      float nearest = 1e30f;
      int hit = -2;
      for (size_t index = 0; index < boxes.size(); ++index) {
        float entry = 0.f;
        if (HitBox(boxes[index], {0, 0, 0}, direction, &entry) && entry < nearest) {
          nearest = entry;
          hit = static_cast<int>(index);
        }
      }
      if (direction[1] < 0.f && (GROUND_Y / direction[1]) < nearest) {
        nearest = GROUND_Y / direction[1];
        hit = -1;
      }
      if (hit == -2) continue;
      const Vector p = {direction[0] * nearest, direction[1] * nearest, direction[2] * nearest};
      const auto clip = Project(camera, p);
      const size_t pixel = (y * w) + x;
      picture.depth[pixel] = clip[2] / clip[3];
      picture.hit[pixel] = hit;
      picture.position[pixel] = p;
    }
  }
  return picture;
}

// The first box (of `kinds`) the ray from `from` along `direction` meets within `reach`: its index (or -1) and distance.
static int Occluder(std::span<const Box> boxes, const Vector& from, const Vector& direction, float reach, float* distance) {
  int found = -1;
  float nearest = reach;
  const Vector start = {from[0] + (direction[0] * 1e-3f), from[1] + (direction[1] * 1e-3f), from[2] + (direction[2] * 1e-3f)};
  for (size_t index = 0; index < boxes.size(); ++index) {
    float entry = 0.f;
    if (HitBox(boxes[index], start, direction, &entry) && entry < nearest) {
      nearest = entry;
      found = static_cast<int>(index);
    }
  }
  *distance = nearest;
  return found;
}

// ---- GPU objects ------------------------------------------------------------------------------------------------------

struct Texture {
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> srv;
};

static Texture MakeTexture(UINT width, UINT height, DXGI_FORMAT format, const void* data, UINT pitch, UINT bind = D3D11_BIND_SHADER_RESOURCE) {
  const D3D11_TEXTURE2D_DESC desc = {width, height, 1, 1, format, {1, 0}, D3D11_USAGE_DEFAULT, bind, 0, 0};
  const D3D11_SUBRESOURCE_DATA initial = {data, pitch, 0};
  Texture made;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, (data != nullptr) ? &initial : nullptr, made.texture.GetAddressOf())));
  if ((bind & D3D11_BIND_SHADER_RESOURCE) != 0) {
    REQUIRE(SUCCEEDED(device->CreateShaderResourceView(made.texture.Get(), nullptr, made.srv.GetAddressOf())));
  }
  return made;
}

static ComPtr<ID3D11Buffer> MakeBuffer(UINT bytes, UINT bind, const void* data, UINT misc = 0, UINT stride = 0) {
  const D3D11_BUFFER_DESC desc = {bytes, D3D11_USAGE_DEFAULT, bind, 0, misc, stride};
  const D3D11_SUBRESOURCE_DATA initial = {data, 0, 0};
  ComPtr<ID3D11Buffer> buffer;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&desc, (data != nullptr) ? &initial : nullptr, buffer.GetAddressOf())));
  return buffer;
}

// A texture's texels (`T` each) through a staging copy.
template <typename T>
static std::vector<T> ReadTexture(ID3D11Resource* resource) {
  ComPtr<ID3D11Texture2D> texture;
  REQUIRE(SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(texture.GetAddressOf()))));
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.MiscFlags = 0;
  ComPtr<ID3D11Texture2D> staging;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf())));
  context->CopyResource(staging.Get(), texture.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  REQUIRE(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
  std::vector<T> texels(desc.Width * desc.Height);
  for (UINT y = 0; y < desc.Height; ++y) {
    std::memcpy(texels.data() + (y * desc.Width), static_cast<const uint8_t*>(mapped.pData) + (y * mapped.RowPitch), desc.Width * sizeof(T));
  }
  context->Unmap(staging.Get(), 0);
  return texels;
}
template <typename T>
static std::vector<T> ReadView(ID3D11View* view) {
  ComPtr<ID3D11Resource> resource;
  view->GetResource(resource.GetAddressOf());
  return ReadTexture<T>(resource.Get());
}
template <typename T>
static std::vector<T> ReadBuffer(ID3D11Buffer* buffer) {
  D3D11_BUFFER_DESC desc = {};
  buffer->GetDesc(&desc);
  const D3D11_BUFFER_DESC staging_desc = {desc.ByteWidth, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ,
                                         desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, desc.StructureByteStride};
  ComPtr<ID3D11Buffer> staging;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&staging_desc, nullptr, staging.GetAddressOf())));
  context->CopyResource(staging.Get(), buffer);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  REQUIRE(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
  std::vector<T> values(desc.ByteWidth / sizeof(T));
  std::memcpy(values.data(), mapped.pData, values.size() * sizeof(T));
  context->Unmap(staging.Get(), 0);
  return values;
}

// The frame's inputs as the sun draw gives them: the depth (as the depth-stencil copy), the camera in Attila's layout
// (c0 its position, c5 projection, c9 view projection, c17 and c21 their inverses), the sun's travel direction at float 1
// of lighting_VS_PS (Attila's), the G-buffer's colour (square roots: gamma 2), the banners' depth (sss_thin's: 1 none).
struct Frame {
  Camera camera;
  Picture picture;
  Vector sun = {};
  Texture depth;
  ComPtr<ID3D11Buffer> camera_constants;
  ComPtr<ID3D11Buffer> lighting;
  UINT sun_direction = 1;  // its first float in `lighting` (Attila's 1; Pharaoh's 4)
  Texture colour;
  Texture thin;
};

static Frame MakeFrame(UINT width, UINT height, std::span<const Box> boxes, const Vector& sun, float jitter = 0.f, float tilt_degrees = 25.f,
                       float far_plane = FAR_PLANE) {
  Frame frame = {.camera = MakeCamera(width, height, tilt_degrees, far_plane), .sun = Normalize(sun)};
  frame.camera.jitter = jitter;
  frame.picture = Trace(frame.camera, boxes);
  frame.depth = MakeTexture(width, height, DXGI_FORMAT_R32_FLOAT, frame.picture.depth.data(), width * 4);
  std::array<float, 30 * 4> constants = {};
  const auto put = [&](UINT base, const Matrix& m) {
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j) constants[((base + j) * 4) + i] = m[i][j];
    }
  };
  put(5, frame.camera.projection);
  put(9, frame.camera.view_projection);
  put(17, Inverse(frame.camera.projection));
  put(21, Inverse(frame.camera.view_projection));
  frame.camera_constants = MakeBuffer(sizeof(constants), D3D11_BIND_CONSTANT_BUFFER, constants.data());
  std::array<float, 16> lighting = {};
  std::copy(frame.sun.begin(), frame.sun.end(), lighting.begin() + 1);
  frame.lighting = MakeBuffer(sizeof(lighting), D3D11_BIND_CONSTANT_BUFFER, lighting.data());
  std::vector<uint32_t> colour(width * height);
  std::vector<float> thin(width * height, 1.f);
  for (size_t pixel = 0; pixel < colour.size(); ++pixel) {
    const int hit = frame.picture.hit[pixel];
    const Vector albedo = (hit >= 0) ? boxes[hit].albedo : Vector{0.5f, 0.5f, 0.5f};
    uint32_t packed = 0xFF000000u;
    for (int channel = 0; channel < 3; ++channel) {
      packed |= static_cast<uint32_t>(std::lround(std::sqrt(albedo[channel]) * 255.f)) << (8 * channel);
    }
    colour[pixel] = packed;
    if (hit >= 0 && boxes[hit].kind == Kind::THIN) thin[pixel] = frame.picture.depth[pixel];
  }
  frame.colour = MakeTexture(width, height, DXGI_FORMAT_R8G8B8A8_UNORM, colour.data(), width * 4);
  frame.thin = MakeTexture(width, height, DXGI_FORMAT_R32_FLOAT, thin.data(), width * 4);
  return frame;
}

// The foliage's depth pass for sss_depth: the depth before it (without the foliage boxes) and after (with them).
struct Foliage {
  Texture before;
  Texture after;
  ID3D11ShaderResourceView* material = nullptr;  // the G-buffer's channel 3 marking it instead (foliage_material_bit)
};

// One frame of the module: its settings, then the march at the sun's draw (Compute; the banners' depth this frame's where
// `thin` is set; the foliage's copies where given). Returns what the sun draws read (the softened occlusion where sss_blur
// or sss_soft ran).
static ID3D11ShaderResourceView* RunFrame(const Frame& frame, const sss::Settings& settings, bool thin = false, const Foliage* foliage = nullptr) {
  auto& runtime = sss::internal::runtime;
  auto& objects = runtime.objects;
  ++runtime.frame;
  runtime.settings = settings;
  runtime.display_width = frame.camera.width;
  runtime.display_height = frame.camera.height;
  REQUIRE(sss::internal::Prepare(device.Get(), frame.camera.width, frame.camera.height));
  if (thin) {
    objects.thin_texture = frame.thin.texture;
    objects.thin_srv = frame.thin.srv;
    runtime.thin_frame = runtime.frame;
  }
  if (foliage != nullptr && foliage->material == nullptr) {
    objects.before_foliage_srv = foliage->before.srv;
    objects.after_foliage_srv = foliage->after.srv;
    runtime.before_foliage_frame = runtime.frame;
    runtime.after_foliage_frame = runtime.frame;
  }
  sss::internal::Compute(context.Get(), frame.depth.srv.Get(), false, frame.camera_constants.Get(), frame.lighting.Get(), frame.sun_direction,
                         settings.coloured ? frame.colour.srv.Get() : nullptr, (foliage != nullptr) ? foliage->material : nullptr);
  runtime.computed_frame = runtime.frame;
  return (runtime.soft_frame == runtime.frame) ? objects.soft_srv.Get() : objects.occlusion_srv.Get();
}

static sss::Settings Defaults() { return {.enabled = true}; }

// Ground pixels wrong against the reference (occlusion over half where it is lit, under half where it is shadowed).
static int WrongPixels(const Frame& frame, std::span<const Box> boxes, const std::vector<uint8_t>& occlusion) {
  int wrong = 0;
  const Vector to_sun = {-frame.sun[0], -frame.sun[1], -frame.sun[2]};
  for (size_t pixel = 0; pixel < occlusion.size(); ++pixel) {
    if (frame.picture.hit[pixel] != -1) continue;
    float distance = 0.f;
    const bool shadowed = Occluder(boxes, frame.picture.position[pixel], to_sun, 1e30f, &distance) >= 0;
    if (shadowed != (occlusion[pixel] > 127)) ++wrong;
  }
  return wrong;
}

// ---- Cases ------------------------------------------------------------------------------------------------------------

static const std::array<Box, 1> BOX_SCENE = {{{.low = {-1.f, -2.f, 9.f}, .high = {1.f, 1.f, 11.f}}}};
static const Vector BOX_SUN = {0.6f, -0.5f, 0.6f};

// Bend's waves near a quadrant's diagonal can write a pixel twice in one dispatch, and WARP runs the groups in any
// order: such a pixel can differ between two runs of the same shaders (today's too). Both sides run RUNS times, in turns;
// a pixel is apart when no run of one gives a value a run of the other gave. `same` false: the sides must differ (more
// than 20 pixels apart); `same` true: more than `least` pixels over half (the picture has shadows).
constexpr int RUNS = 8;
static void CompareRuns(const char* what, const std::vector<std::vector<uint8_t>>& ours, const std::vector<std::vector<uint8_t>>& theirs,
                        bool same = true, int least = 100) {
  int varying = 0;
  int apart = 0;
  int over_half = 0;
  for (size_t pixel = 0; pixel < ours[0].size(); ++pixel) {
    bool stable = true;
    bool shared = false;
    for (int run = 0; run < RUNS; ++run) {
      stable = stable && ours[run][pixel] == ours[0][pixel] && theirs[run][pixel] == ours[0][pixel];
      for (int other = 0; other < RUNS; ++other) shared = shared || ours[run][pixel] == theirs[other][pixel];
    }
    varying += stable ? 0 : 1;
    apart += shared ? 0 : 1;
    over_half += (stable && ours[0][pixel] > 127) ? 1 : 0;
  }
  std::printf("   %s: %d pixels over half the same, %d apart, %d varying between runs\n", what, over_half, apart, varying);
  if (same) {
    CHECK(apart == 0);
    CHECK(over_half > least && varying < static_cast<int>(ours[0].size() / 500));
  } else {
    CHECK(apart > 20);
  }
}

static void TestRowsOff(const std::string& before_folder) {
  std::printf("1. rows off\n");
  MakeDevice(D3D_DRIVER_TYPE_WARP);
  const Frame frame = MakeFrame(480, 270, BOX_SCENE, BOX_SUN);
  auto& objects = sss::internal::runtime.objects;
  for (const float softness : {0.f, 2.f}) {
    sss::Settings settings = Defaults();
    settings.softness = softness;
    // fc24dbce's passes in place of the module's own (the module's bindings), in turns.
    const auto load = [&](const char* name) {
      const auto code = ReadFile(before_folder + "\\" + name + ".cso");
      REQUIRE(!code.empty());
      ComPtr<ID3D11ComputeShader> shader;
      REQUIRE(SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, shader.GetAddressOf())));
      return shader;
    };
    REQUIRE(sss::internal::Prepare(device.Get(), frame.camera.width, frame.camera.height));
    const std::array<ComPtr<ID3D11ComputeShader>, 4> module_shaders = {objects.depth_copy, objects.setup, objects.bend, objects.blur};
    const std::array<ComPtr<ID3D11ComputeShader>, 4> before_shaders = {load("sss_depth"), load("sss_setup"), load("sss_bend"), load("sss_blur")};
    std::vector<std::vector<uint8_t>> ours;
    std::vector<std::vector<uint8_t>> theirs;
    std::array<std::vector<uint8_t>, 2> parameters;
    std::array<std::vector<uint8_t>, 2> arguments;
    for (int run = 0; run < RUNS * 2; ++run) {
      const auto& shaders = ((run % 2) == 0) ? module_shaders : before_shaders;
      objects.depth_copy = shaders[0];
      objects.setup = shaders[1];
      objects.bend = shaders[2];
      objects.blur = shaders[3];
      auto& results = ((run % 2) == 0) ? ours : theirs;
      results.push_back(ReadView<uint8_t>(RunFrame(frame, settings)));
      parameters[run % 2] = ReadBuffer<uint8_t>(objects.parameters.Get());
      arguments[run % 2] = ReadBuffer<uint8_t>(objects.arguments.Get());
    }
    objects.depth_copy = module_shaders[0];
    objects.setup = module_shaders[1];
    objects.bend = module_shaders[2];
    objects.blur = module_shaders[3];
    // sss_setup's output, the march's whole input from it: to the bit.
    CHECK(parameters[0] == parameters[1]);
    CHECK(arguments[0] == arguments[1]);
    CompareRuns((softness == 0.f) ? "against fc24dbce's" : "Softness 2 px against fc24dbce's", ours, theirs);
  }
  // The extended march with nothing to do: banners on but none in view, foliage marked (a bush's depth pass).
  const std::array<Box, 2> bush_scene = {{BOX_SCENE[0], {.low = {2.f, -2.f, 7.f}, .high = {3.f, -1.f, 8.f}, .kind = Kind::FOLIAGE}}};
  const Frame bush = MakeFrame(480, 270, bush_scene, BOX_SUN);
  const Frame without = MakeFrame(480, 270, std::span(bush_scene).first(1), BOX_SUN);
  const Foliage foliage = {.before = without.depth, .after = bush.depth};
  sss::Settings plain_settings = Defaults();
  plain_settings.foliage_light = 0.5f;
  sss::Settings extended_settings = plain_settings;
  extended_settings.thin_light = 0.35f;
  std::vector<std::vector<uint8_t>> plain;
  std::vector<std::vector<uint8_t>> extended;
  for (int run = 0; run < RUNS; ++run) {
    plain.push_back(ReadView<uint8_t>(RunFrame(bush, plain_settings, false, &foliage)));
    extended.push_back(ReadView<uint8_t>(RunFrame(bush, extended_settings, true, &foliage)));
    CHECK(sss::internal::runtime.objects.uploaded_extended.thin_light == 0.35f);
  }
  const auto lit_through = std::ranges::count_if(plain[0], [](uint8_t value) { return value > 40 && value < 200; });
  std::printf("   foliage at 0.5: %zu half-lit pixels\n", static_cast<size_t>(lit_through));
  CHECK(lit_through > 50);
  // The same bush marked by the G-buffer instead (Warhammer III: channel 3's alpha byte, bit 128; the rest 5, its rigid
  // objects'): the same occlusion as the depth copies'.
  {
    const auto bush_depth = ReadView<float>(bush.depth.srv.Get());
    const auto plain_depth = ReadView<float>(without.depth.srv.Get());
    std::vector<uint32_t> bytes(bush_depth.size());
    for (size_t index = 0; index < bytes.size(); ++index) bytes[index] = ((bush_depth[index] != plain_depth[index]) ? 132u : 5u) << 24;
    const Texture material = MakeTexture(480, 270, DXGI_FORMAT_R8G8B8A8_UNORM, bytes.data(), 480 * 4);
    const Foliage marked = {.material = material.srv.Get()};
    sss::foliage_material_bit = 128;
    std::vector<std::vector<uint8_t>> by_material;
    for (int run = 0; run < RUNS; ++run) by_material.push_back(ReadView<uint8_t>(RunFrame(bush, plain_settings, false, &marked)));
    sss::foliage_material_bit = 0;
    CompareRuns("foliage marked by the G-buffer's material byte against the depth copies", by_material, plain);
    // Pharaoh: its bit 128 on leaves, tree cards and the far forest's blocks (128) and grass (160); every other byte its drawn
    // G-buffer shaders write (branches 0, rigid 1, skin 8, banners 10, terrain 21, units and props 33, water 64), in turns.
    // CONTROL (must differ): the water's SSR bit (64) taken as the mark.
    constexpr std::array<uint32_t, 2> PHARAOH_FOLIAGE = {128u, 160u};
    constexpr std::array<uint32_t, 7> PHARAOH_OTHERS = {0u, 1u, 8u, 10u, 21u, 33u, 64u};
    for (size_t index = 0; index < bytes.size(); ++index) {
      bytes[index] = ((bush_depth[index] != plain_depth[index]) ? PHARAOH_FOLIAGE[index % PHARAOH_FOLIAGE.size()] : PHARAOH_OTHERS[index % PHARAOH_OTHERS.size()]) << 24;
    }
    const Texture pharaoh_material = MakeTexture(480, 270, DXGI_FORMAT_R8G8B8A8_UNORM, bytes.data(), 480 * 4);
    const Foliage pharaoh_marked = {.material = pharaoh_material.srv.Get()};
    std::vector<std::vector<uint8_t>> by_pharaoh_bit;
    std::vector<std::vector<uint8_t>> by_water_bit;
    sss::foliage_material_bit = 128;
    for (int run = 0; run < RUNS; ++run) by_pharaoh_bit.push_back(ReadView<uint8_t>(RunFrame(bush, plain_settings, false, &pharaoh_marked)));
    sss::foliage_material_bit = 64;
    for (int run = 0; run < RUNS; ++run) by_water_bit.push_back(ReadView<uint8_t>(RunFrame(bush, plain_settings, false, &pharaoh_marked)));
    sss::foliage_material_bit = 0;
    CompareRuns("Pharaoh's material bytes, bit 128, against the depth copies", by_pharaoh_bit, plain);
    CompareRuns("CONTROL Pharaoh's material bytes, bit 64 (water), against the depth copies", by_water_bit, plain, false);
  }
  CompareRuns("extended march with nothing to do against the plain one", extended, plain);

  // Below 100% (the render the top-left of the display's textures, 960 x 540) with the counts a host scales by the render
  // share (the UE hosts', the add-on's before O-SS5: scale_control.samples_by_share): the sun's 60 samples are 30 at 50%,
  // 35 at 58% (557 x 313) and 46 at 77% (739 x 416), their fade-outs (32 after 16 hard ones) 14, 19 and 30. Both marches
  // take them in fours (X27): the plain march's with 32, 36 and 48 samples, fade-outs 12, 16 and 28. CONTROL (must
  // differ): fc24dbce's plain march (before X27) with the odd counts against its own with the counts in fours.
  struct Scale {
    UINT width;
    UINT height;
    float samples;  // the plain march's Length for the rounded count at this scale
    float fade;
  };
  // The box, a wall behind it and a pole beside it.
  const std::array<Box, 3> casters = {{BOX_SCENE[0], {.low = {-6.f, -2.f, 20.f}, .high = {6.f, 0.f, 22.f}}, {.low = {3.f, -2.f, 10.f}, .high = {3.2f, 2.f, 10.2f}}}};
  const auto unrounded_code = ReadFile(before_folder + "\\sss_bend.cso");
  REQUIRE(!unrounded_code.empty());
  ComPtr<ID3D11ComputeShader> unrounded_bend;
  REQUIRE(SUCCEEDED(device->CreateComputeShader(unrounded_code.data(), unrounded_code.size(), nullptr, unrounded_bend.GetAddressOf())));
  for (const auto& scale : {Scale{480, 270, 64.f, 12.f}, Scale{557, 313, 62.f, 16.f}, Scale{739, 416, 62.f, 28.f}}) {
    const Frame render = MakeFrame(scale.width, scale.height, casters, BOX_SUN);
    Frame scaled = MakeFrame(960, 540, {}, BOX_SUN);  // the display's size; its banners' depth all far
    std::vector<float> depth(960 * 540, 1.f);
    for (UINT y = 0; y < scale.height; ++y) {
      std::copy_n(render.picture.depth.begin() + (y * scale.width), scale.width, depth.begin() + (y * 960));
    }
    scaled.depth = MakeTexture(960, 540, DXGI_FORMAT_R32_FLOAT, depth.data(), 960 * 4);
    scaled.camera_constants = render.camera_constants;
    twattila::frame_history::render_width = scale.width;
    twattila::frame_history::render_height = scale.height;
    sss::Settings odd = Defaults();
    odd.samples = 60.f;
    odd.hard_samples = 16.f;
    odd.fade_samples = 32.f;
    sss::Settings rounded = odd;
    rounded.samples = scale.samples;
    rounded.fade_samples = scale.fade;
    sss::Settings odd_extended = odd;
    odd_extended.thin_light = 0.35f;
    std::vector<std::vector<uint8_t>> odd_plain;
    std::vector<std::vector<uint8_t>> rounded_plain;
    std::vector<std::vector<uint8_t>> rounded_extended;
    std::vector<std::vector<uint8_t>> unrounded_plain;
    std::vector<std::vector<uint8_t>> unrounded_rounded;
    const ComPtr<ID3D11ComputeShader> module_bend = objects.bend_steps;
    sss::internal::scale_control = {.samples_by_share = true, .thickness_by_share = true};
    for (int run = 0; run < RUNS; ++run) {
      odd_plain.push_back(ReadView<uint8_t>(RunFrame(scaled, odd)));
      rounded_plain.push_back(ReadView<uint8_t>(RunFrame(scaled, rounded)));
      rounded_extended.push_back(ReadView<uint8_t>(RunFrame(scaled, odd_extended, true)));
      objects.bend_steps = unrounded_bend;
      unrounded_rounded.push_back(ReadView<uint8_t>(RunFrame(scaled, rounded)));
      unrounded_plain.push_back(ReadView<uint8_t>(RunFrame(scaled, odd)));
      objects.bend_steps = module_bend;
    }
    sss::internal::scale_control = {};
    std::printf("   %u x %u of 960 x 540: the module's %.0f samples, fade-out %.0f\n", scale.width, scale.height, objects.uploaded.samples,
                objects.uploaded.fade_samples);
    CHECK(objects.uploaded.samples == std::round(60.f * static_cast<float>(scale.width) / 960.f) && std::fmod(objects.uploaded.samples, 4.f) != 0.f);
    // Under O-SS5's screen steps these x share counts reach share x the length on screen: fewer shadow pixels (30 the least).
    CompareRuns("   extended march in fours against the plain one's with the counts in fours", rounded_extended, rounded_plain, true, 30);
    CompareRuns("   the plain march's with the counts in fours against its own with the odd ones", rounded_plain, odd_plain, true, 30);
    CompareRuns("   CONTROL fc24dbce's plain march with the counts in fours against its own with the odd ones", unrounded_rounded, unrounded_plain, false);
  }
  twattila::frame_history::render_width = 0;
  twattila::frame_history::render_height = 0;
}

static void TestAutoThickness() {
  std::printf("2. Auto Thickness\n");
  MakeDevice(D3D_DRIVER_TYPE_WARP);
  // The sun shines past them towards the camera: their shadows fall in front of them.
  const Vector sun = {0.25f, -0.55f, -0.8f};
  struct Case {
    const char* name;
    Box box;
    bool must_improve;
  };
  const std::array<Case, 3> cases = {{
      {"deep box 4 x 3 x 4 m", {.low = {-2.f, -2.f, 14.f}, .high = {2.f, 1.f, 18.f}}, true},
      {"thick wall 8 x 2 x 2 m", {.low = {-4.f, -2.f, 14.f}, .high = {4.f, 0.f, 16.f}}, true},
      {"pole 0.2 x 3 x 0.2 m", {.low = {-0.1f, -2.f, 12.f}, .high = {0.1f, 1.f, 12.2f}}, false},
  }};
  for (const auto& test_case : cases) {
    const std::array<Box, 1> boxes = {test_case.box};
    const Frame frame = MakeFrame(480, 270, boxes, sun);
    sss::Settings settings = Defaults();
    const int off = WrongPixels(frame, boxes, ReadView<uint8_t>(RunFrame(frame, settings)));
    settings.auto_thickness = true;
    const int on = WrongPixels(frame, boxes, ReadView<uint8_t>(RunFrame(frame, settings)));
    std::printf("   %s: wrong ground pixels %d off, %d on\n", test_case.name, off, on);
    if (test_case.must_improve) {
      CHECK(on < off);
    } else {
      CHECK(on <= (off * 105 / 100) + 20);
    }
  }
}

static void TestBanner() {
  std::printf("3. Light Through Banners, Coloured Shadows\n");
  MakeDevice(D3D_DRIVER_TYPE_WARP);
  const Vector sun = {0.2f, -0.6f, -0.77f};
  std::array<Box, 1> boxes = {{{.low = {-1.f, -0.6f, 12.f}, .high = {1.f, 1.4f, 12.05f}, .kind = Kind::THIN, .albedo = {0.8f, 0.1f, 0.1f}}}};
  const Frame frame = MakeFrame(480, 270, boxes, sun);
  sss::internal::runtime.albedo_power = 2.f;
  sss::Settings settings = Defaults();
  settings.thin_light = 0.35f;
  settings.coloured = true;
  const auto occlusion = ReadView<uint8_t>(RunFrame(frame, settings, true));
  REQUIRE(sss::internal::runtime.objects.tint_srv);
  const auto tint = ReadView<uint32_t>(sss::internal::runtime.objects.tint_srv.Get());
  settings.thin_light = 0.f;
  settings.coloured = false;
  const auto solid = ReadView<uint8_t>(RunFrame(frame, settings));
  // The banner's shadow on the ground, away from its edges (3 pixels in), and the ground in the sun.
  const Vector to_sun = {-frame.sun[0], -frame.sun[1], -frame.sun[2]};
  const UINT w = frame.camera.width;
  const UINT h = frame.camera.height;
  std::vector<uint8_t> shadow(w * h, 0);
  for (size_t pixel = 0; pixel < shadow.size(); ++pixel) {
    float distance = 0.f;
    if (frame.picture.hit[pixel] == -1 && Occluder(boxes, frame.picture.position[pixel], to_sun, 1e30f, &distance) >= 0) shadow[pixel] = 1;
  }
  struct Means {
    double occlusion = 0.0;
    double solid = 0.0;
    std::array<double, 3> filter = {};
    int inner = 0;
    uint32_t sunlit_tint = 0;
  };
  const auto measure = [&](const std::vector<uint8_t>& occlusion, const std::vector<uint32_t>& tint) {
    Means means;
    for (UINT y = 3; y + 3 < h; ++y) {
      for (UINT x = 3; x + 3 < w; ++x) {
        bool all = true;
        bool none = true;
        for (int dy = -3; dy <= 3; ++dy) {
          for (int dx = -3; dx <= 3; ++dx) {
            const uint8_t value = shadow[((y + dy) * w) + x + dx];
            all = all && value != 0;
            none = none && value == 0;
          }
        }
        const size_t pixel = (y * w) + x;
        if (all) {
          means.occlusion += occlusion[pixel] / 255.0;
          means.solid += solid[pixel] / 255.0;
          for (int channel = 0; channel < 3; ++channel) means.filter[channel] += ((tint[pixel] >> (8 * channel)) & 0xFF) / 255.0;
          ++means.inner;
        } else if (none && frame.picture.hit[pixel] == -1) {
          means.sunlit_tint = std::max(means.sunlit_tint, tint[pixel] & 0xFFFFFFu);
        }
      }
    }
    REQUIRE(means.inner > 50);
    means.occlusion /= means.inner;
    means.solid /= means.inner;
    for (double& channel : means.filter) channel /= means.inner;
    return means;
  };
  const Means attila = measure(occlusion, tint);
  std::printf("   %d shadow pixels: occlusion %.3f (solid %.3f), filter %.3f %.3f %.3f (expected 0 0.875 0.875), in the sun %06X\n", attila.inner,
              attila.occlusion, attila.solid, attila.filter[0], attila.filter[1], attila.filter[2], attila.sunlit_tint);
  // Bend's 4 lanes average a thin sheet's shadow (a lane shadows only where one of its samples meets it): the banner's
  // occlusion is its solid one's x (1 - its share).
  CHECK(attila.solid > 0.3);
  CHECK(std::fabs((attila.occlusion / attila.solid) - 0.65) < 0.12);
  CHECK(attila.filter[0] < 0.1 && attila.filter[1] > 0.6 && attila.filter[2] > 0.6);
  CHECK(attila.sunlit_tint == 0);

  // Pharaoh: the sun's direction at float 4 of lighting_VS_PS (its c1; float 1 holds another direction here, a decoy), the
  // G-buffer's colour an sRGB texture (read linear: albedo_power 1). The same occlusion and filter within 0.01. CONTROL
  // (must differ): the decoy read, at Attila's float 1.
  Frame pharaoh = frame;
  std::array<float, 16> lighting = {};
  const Vector decoy = Normalize({-0.6f, -0.6f, 0.5f});
  std::copy(decoy.begin(), decoy.end(), lighting.begin() + 1);
  std::copy(frame.sun.begin(), frame.sun.end(), lighting.begin() + 4);
  pharaoh.lighting = MakeBuffer(sizeof(lighting), D3D11_BIND_CONSTANT_BUFFER, lighting.data());
  pharaoh.sun_direction = 4;
  std::vector<uint32_t> srgb(w * h);
  for (size_t pixel = 0; pixel < srgb.size(); ++pixel) {
    const int hit = frame.picture.hit[pixel];
    const Vector albedo = (hit >= 0) ? boxes[hit].albedo : Vector{0.5f, 0.5f, 0.5f};
    uint32_t packed = 0xFF000000u;
    for (int channel = 0; channel < 3; ++channel) {
      const float linear = albedo[channel];
      const float encoded = (linear <= 0.0031308f) ? (12.92f * linear) : ((1.055f * std::pow(linear, 1.f / 2.4f)) - 0.055f);
      packed |= static_cast<uint32_t>(std::lround(encoded * 255.f)) << (8 * channel);
    }
    srgb[pixel] = packed;
  }
  pharaoh.colour = MakeTexture(w, h, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, srgb.data(), w * 4);
  sss::internal::runtime.albedo_power = 1.f;
  settings.thin_light = 0.35f;
  settings.coloured = true;
  const auto pharaoh_occlusion = ReadView<uint8_t>(RunFrame(pharaoh, settings, true));
  const Means pharaoh_means = measure(pharaoh_occlusion, ReadView<uint32_t>(sss::internal::runtime.objects.tint_srv.Get()));
  pharaoh.sun_direction = 1;
  const Means decoy_means = measure(ReadView<uint8_t>(RunFrame(pharaoh, settings, true)), ReadView<uint32_t>(sss::internal::runtime.objects.tint_srv.Get()));
  sss::internal::runtime.albedo_power = 2.f;
  std::printf("   Pharaoh (sun at float 4, sRGB colour): occlusion %.3f, filter %.3f %.3f %.3f, in the sun %06X; CONTROL the decoy at float 1: occlusion %.3f\n",
              pharaoh_means.occlusion, pharaoh_means.filter[0], pharaoh_means.filter[1], pharaoh_means.filter[2], pharaoh_means.sunlit_tint,
              decoy_means.occlusion);
  CHECK(std::fabs(pharaoh_means.occlusion - attila.occlusion) < 0.01);
  for (int channel = 0; channel < 3; ++channel) CHECK(std::fabs(pharaoh_means.filter[channel] - attila.filter[channel]) < 0.01);
  CHECK(pharaoh_means.sunlit_tint == 0);
  CHECK(std::fabs(decoy_means.occlusion - attila.occlusion) > 0.1);
}

// The device's CreateTexture2D (TestSofter puts this in its vtable for a frame): R16_FLOAT, the penumbra's format, failing.
using CreateTexture2D = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const D3D11_TEXTURE2D_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture2D**);
static CreateTexture2D create_texture_2d = nullptr;
static HRESULT STDMETHODCALLTYPE CreateTexture2DFailingPenumbra(ID3D11Device* self, const D3D11_TEXTURE2D_DESC* desc, const D3D11_SUBRESOURCE_DATA* data,
                                                                ID3D11Texture2D** texture) {
  return (desc->Format == DXGI_FORMAT_R16_FLOAT) ? E_OUTOFMEMORY : create_texture_2d(self, desc, data, texture);
}

static void TestSofter() {
  std::printf("4. Softer Away from the Caster\n");
  MakeDevice(D3D_DRIVER_TYPE_WARP);
  const Vector sun = {0.8f, -0.6f, 0.f};
  const std::array<Box, 1> boxes = {{{.low = {-0.1f, -2.f, 14.9f}, .high = {0.1f, 2.f, 15.1f}}}};
  const UINT w = 960;
  const UINT h = 540;
  const Frame frame = MakeFrame(w, h, boxes, sun);
  sss::Settings settings = Defaults();
  settings.sun_disk = 2.f;
  const auto soft = ReadView<uint8_t>(RunFrame(frame, settings));
  const auto penumbra = ReadView<uint16_t>(sss::internal::runtime.objects.penumbra_srv.Get());
  const auto half_to_float = [](uint16_t half) {
    const uint32_t sign = (half & 0x8000u) << 16;
    const uint32_t exponent = (half >> 10) & 0x1Fu;
    const uint32_t mantissa = half & 0x3FFu;
    const uint32_t bits = (exponent == 0) ? sign : (sign | ((exponent + 112u) << 23) | (mantissa << 13));
    float value = 0.f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  };
  const Vector to_sun = {-frame.sun[0], -frame.sun[1], -frame.sun[2]};
  const float tangent = std::tan(1.f * 3.14159265f / 180.f);
  std::vector<float> ratios;
  double near_radius = 0.0;
  double far_radius = 0.0;
  int near_count = 0;
  int far_count = 0;
  for (size_t pixel = 0; pixel < penumbra.size(); ++pixel) {
    float distance = 0.f;
    if (frame.picture.hit[pixel] != -1 || Occluder(boxes, frame.picture.position[pixel], to_sun, 1e30f, &distance) < 0) continue;
    const float radius = half_to_float(penumbra[pixel]);
    if (radius <= 0.f) continue;
    const Vector& p = frame.picture.position[pixel];
    const float view_z = (p[1] * frame.camera.view[1][2]) + (p[2] * frame.camera.view[2][2]);
    const float expected = distance * tangent * 0.5f * static_cast<float>(h) * frame.camera.projection[1][1] / view_z;
    if (expected >= sss::internal::SOFT_RADIUS_2160 * static_cast<float>(h) / 2160.f) continue;
    ratios.push_back(radius / expected);
    // Along the shadow: its first metre and its last.
    if (p[0] < 1.5f) {
      near_radius += radius;
      ++near_count;
    } else if (p[0] > 3.5f) {
      far_radius += radius;
      ++far_count;
    }
  }
  REQUIRE(ratios.size() > 50 && near_count > 5 && far_count > 5);
  std::ranges::nth_element(ratios, ratios.begin() + static_cast<ptrdiff_t>(ratios.size() / 2));
  const float median = ratios[ratios.size() / 2];
  near_radius /= near_count;
  far_radius /= far_count;
  // The softened shadow's edge: pixels between a tenth and nine tenths shadowed near the pole and far from it.
  int near_edge = 0;
  int far_edge = 0;
  for (size_t pixel = 0; pixel < soft.size(); ++pixel) {
    if (frame.picture.hit[pixel] != -1 || soft[pixel] < 26 || soft[pixel] > 229) continue;
    const float x = frame.picture.position[pixel][0];
    if (x > 0.2f && x < 1.5f) ++near_edge;
    if (x > 3.5f && x < 4.8f) ++far_edge;
  }
  std::printf("   %zu shadowed pixels: penumbra over the reference median %.3f; radius %.2f px near, %.2f px far; edge pixels %d near, %d far\n",
              ratios.size(), median, near_radius, far_radius, near_edge, far_edge);
  CHECK(median > 0.75f && median < 1.25f);
  CHECK(far_radius > near_radius * 1.5);
  CHECK(far_edge > near_edge);

  // Softness 0 and no penumbra to write (its texture not made: the device's CreateTexture2D failing R16_FLOAT, the
  // penumbra's format, for a frame): no pass softened this frame's occlusion, the sun draws read it, not the softened one
  // of the frame before.
  auto& objects = sss::internal::runtime.objects;
  CHECK(sss::internal::runtime.soft_frame == sss::internal::runtime.frame);
  objects.penumbra_uav.Reset();
  objects.penumbra_srv.Reset();
  constexpr size_t CREATE_TEXTURE_2D = 5;  // ID3D11Device's vtable: IUnknown's 3, CreateBuffer, CreateTexture1D
  void** vtable = *reinterpret_cast<void***>(device.Get());
  const auto patch = [&](void* function) {
    DWORD protection = 0;
    REQUIRE(VirtualProtect(&vtable[CREATE_TEXTURE_2D], sizeof(void*), PAGE_EXECUTE_READWRITE, &protection));
    vtable[CREATE_TEXTURE_2D] = function;
    VirtualProtect(&vtable[CREATE_TEXTURE_2D], sizeof(void*), protection, &protection);
  };
  create_texture_2d = reinterpret_cast<CreateTexture2D>(vtable[CREATE_TEXTURE_2D]);
  patch(reinterpret_cast<void*>(&CreateTexture2DFailingPenumbra));
  const auto* read = RunFrame(frame, settings);
  patch(reinterpret_cast<void*>(create_texture_2d));
  std::printf("   no penumbra texture, Softness 0: the sun draws read the %s occlusion\n", (read == objects.soft_srv.Get()) ? "SOFTENED" : "plain");
  CHECK(!objects.penumbra_uav && objects.uploaded_extended.sun_tangent == 0.f && read == objects.occlusion_srv.Get());
}

// A light draw's instance: its colour (POSITION1) and position and reach (POSITION2), 48 bytes apart.
struct LightInstance {
  std::array<float, 4> colour;
  std::array<float, 4> light;
  std::array<float, 4> other;
};

// Pharaoh's (its light VS E19549A1, vb1 stride 64), in the VS's input order: POSITION1 the colour, POSITION2 position and
// reach, POSITION3 the spot's direction, POSITION4 its cone.
struct PharaohLightInstance {
  std::array<float, 4> colour;
  std::array<float, 4> light;
  std::array<float, 4> spot;
  std::array<float, 4> cone;
};

// Two frames of light draws (`instances` each, in draws of `per_draw`, 0 one draw), the second marching the first's: true
// if the second's first draw bound the lit bits.
template <typename Instance>
static bool LightFrames(const Frame& frame, std::span<const Instance> instances, uint32_t wanted, float range = 0.f, size_t per_draw = 0) {
  const auto buffer = MakeBuffer(static_cast<UINT>(instances.size_bytes()), D3D11_BIND_VERTEX_BUFFER, instances.data());
  sss::Settings settings = Defaults();
  settings.lights = wanted;
  settings.light_range = range;
  const size_t step = (per_draw == 0) ? instances.size() : per_draw;
  bool bound = false;
  for (int repeat = 0; repeat < 2; ++repeat) {
    RunFrame(frame, settings);
    for (size_t first = 0; first < instances.size(); first += step) {
      const bool drawn = lights::OnLightDraw(context.Get(), {.buffer = buffer.Get(),
                                                             .first = static_cast<UINT>(first * sizeof(Instance)),
                                                             .count = static_cast<UINT>(std::min(step, instances.size() - first)),
                                                             .stride = sizeof(Instance),
                                                             .position = offsetof(Instance, light),
                                                             .colour = offsetof(Instance, colour)});
      if (first == 0) bound = drawn;
    }
  }
  return bound;
}

static void TestTorches() {
  std::printf("5. Torch and Fire Shadows\n");
  MakeDevice(D3D_DRIVER_TYPE_WARP);
  const std::array<Box, 1> boxes = {{{.low = {-0.5f, -2.f, 9.5f}, .high = {0.5f, -1.f, 10.5f}}}};
  const Frame frame = MakeFrame(480, 270, boxes, BOX_SUN);
  const LightInstance torch = {.colour = {4.f, 2.f, 0.5f, 0.f}, .light = {2.f, -1.2f, 10.f, 6.f}};
  REQUIRE(LightFrames(frame, std::span(&torch, 1), 4));
  auto& objects = sss::internal::runtime.objects;
  const auto mask = ReadView<uint32_t>(objects.lights.mask_uav.Get());
  const auto list = ReadBuffer<std::array<float, 4>>(objects.lights.list.Get());
  uint32_t count = 0;
  std::memcpy(&count, &list[lights::internal::LIST_LIGHTS][0], sizeof(count));
  CHECK(count == 1 && list[0] == torch.light);
  // The rectangle its reach covers on screen (sss_lights': its box's corners).
  float low_x = 1e30f;
  float low_y = 1e30f;
  float high_x = -1e30f;
  float high_y = -1e30f;
  for (int corner = 0; corner < 8; ++corner) {
    const float reach = torch.light[3];
    const Vector p = {torch.light[0] + (((corner & 1) != 0) ? reach : -reach), torch.light[1] + (((corner & 2) != 0) ? reach : -reach),
                      torch.light[2] + (((corner & 4) != 0) ? reach : -reach)};
    const auto clip = Project(frame.camera, p);
    const float x = ((clip[0] / clip[3] * 0.5f) + 0.5f) * static_cast<float>(frame.camera.width);
    const float y = ((clip[1] / clip[3] * -0.5f) + 0.5f) * static_cast<float>(frame.camera.height);
    low_x = std::min(low_x, x);
    low_y = std::min(low_y, y);
    high_x = std::max(high_x, x);
    high_y = std::max(high_y, y);
  }
  int agree = 0;
  int compared = 0;
  int shadowed = 0;
  int outside_not_lit = 0;
  const Vector light = {torch.light[0], torch.light[1], torch.light[2]};
  for (UINT y = 0; y < frame.camera.height; ++y) {
    for (UINT x = 0; x < frame.camera.width; ++x) {
      const size_t pixel = (y * frame.camera.width) + x;
      const bool inside = static_cast<float>(x) >= std::floor(low_x) - 1.f && static_cast<float>(x) < std::ceil(high_x) + 1.f
                          && static_cast<float>(y) >= std::floor(low_y) - 1.f && static_cast<float>(y) < std::ceil(high_y) + 1.f;
      if (!inside) {
        if (mask[pixel] != UINT32_MAX) ++outside_not_lit;
        continue;
      }
      if (frame.picture.hit[pixel] != -1) continue;
      const Vector& p = frame.picture.position[pixel];
      const float to_light = Distance(p, light);
      if (to_light > torch.light[3]) continue;
      const Vector direction = {(light[0] - p[0]) / to_light, (light[1] - p[1]) / to_light, (light[2] - p[2]) / to_light};
      float distance = 0.f;
      const bool blocked = Occluder(boxes, p, direction, to_light, &distance) >= 0;
      const bool lit = (mask[pixel] & 1u) != 0;
      shadowed += blocked ? 1 : 0;
      agree += (blocked != lit) ? 1 : 0;
      ++compared;
    }
  }
  std::printf("   %d ground pixels in reach (%d shadowed by the reference): %.1f%% agree; %d outside the rectangle not all lit\n", compared,
              shadowed, 100.0 * agree / std::max(compared, 1), outside_not_lit);
  CHECK(compared > 1000 && shadowed > 100);
  CHECK(agree >= compared * 95 / 100);
  CHECK(outside_not_lit == 0);
  // Pharaoh's instances (64 bytes a light): the same light picked and the same lit bits (up to the pixels Bend's waves write
  // twice, under 0.2%).
  const PharaohLightInstance pharaoh_torch = {.colour = torch.colour, .light = torch.light, .spot = {0.f, -1.f, 0.f, 0.f}, .cone = {1.f, 0.f, 0.f, 0.f}};
  REQUIRE(LightFrames(frame, std::span(&pharaoh_torch, 1), 4));
  const auto pharaoh_mask = ReadView<uint32_t>(objects.lights.mask_uav.Get());
  const auto pharaoh_list = ReadBuffer<std::array<float, 4>>(objects.lights.list.Get());
  uint32_t pharaoh_count = 0;
  std::memcpy(&pharaoh_count, &pharaoh_list[lights::internal::LIST_LIGHTS][0], sizeof(pharaoh_count));
  size_t pharaoh_apart = 0;
  for (size_t pixel = 0; pixel < mask.size(); ++pixel) pharaoh_apart += ((mask[pixel] ^ pharaoh_mask[pixel]) & 1u);
  std::printf("   Pharaoh's 64-byte instances: %u light, %s; %zu of %zu pixels' bits apart from the 48-byte ones'\n", pharaoh_count,
              (pharaoh_list[0] == torch.light && pharaoh_list[lights::internal::LIST_LIGHTS + 1][0] == torch.colour[0]) ? "the same" : "ANOTHER",
              pharaoh_apart, mask.size());
  CHECK(pharaoh_count == 1 && pharaoh_list[0] == torch.light && pharaoh_list[lights::internal::LIST_LIGHTS + 1][0] == torch.colour[0]);
  CHECK(pharaoh_apart * 500 < mask.size());

  // 100 lights, one of them twice: the pick as the rays' (rt_core.hpp's score, partial_sort).
  std::mt19937 random(7);
  std::uniform_real_distribution<float> spread(-30.f, 30.f);
  std::uniform_real_distribution<float> unit(0.1f, 4.f);
  std::vector<LightInstance> many;
  for (int index = 0; index < 100; ++index) {
    many.push_back({.colour = {unit(random), unit(random), unit(random), 0.f}, .light = {spread(random), -1.f, 20.f + spread(random), unit(random) * 3.f}});
  }
  many.push_back(many[17]);
  REQUIRE(LightFrames(frame, std::span<const LightInstance>(many), 8));
  const auto picked = ReadBuffer<std::array<float, 4>>(objects.lights.list.Get());
  const auto score = [](const LightInstance& instance) {
    const auto& l = instance.light;
    return std::max({instance.colour[0], instance.colour[1], instance.colour[2]}) * l[3] * l[3] / std::max((l[0] * l[0]) + (l[1] * l[1]) + (l[2] * l[2]), 1.f);
  };
  std::vector<LightInstance> unique(many.begin(), many.end() - 1);
  std::ranges::stable_sort(unique, [&](const LightInstance& a, const LightInstance& b) { return score(a) > score(b); });
  std::memcpy(&count, &picked[lights::internal::LIST_LIGHTS][0], sizeof(count));
  bool same = count == 8;
  for (uint32_t rank = 0; same && rank < 8; ++rank) {
    same = picked[rank] == unique[rank].light && picked[lights::internal::LIST_LIGHTS + 1 + rank][0] == unique[rank].colour[0];
  }
  std::printf("   100 lights + a repeat: the 8 picked are the CPU's, in order: %s\n", same ? "yes" : "NO");
  CHECK(same);
  // Torch and Fire Shadow Range 25 m: the 32 picked of those within it (the camera at the origin), in order.
  constexpr float RANGE = 25.f;
  REQUIRE(LightFrames(frame, std::span<const LightInstance>(many), 32, RANGE));
  const auto ranged = ReadBuffer<std::array<float, 4>>(objects.lights.list.Get());
  std::vector<LightInstance> within;
  std::ranges::copy_if(unique, std::back_inserter(within), [&](const LightInstance& instance) {
    const auto& l = instance.light;
    return (l[0] * l[0]) + (l[1] * l[1]) + (l[2] * l[2]) <= RANGE * RANGE;
  });
  std::memcpy(&count, &ranged[lights::internal::LIST_LIGHTS][0], sizeof(count));
  const auto expected = static_cast<uint32_t>(std::min<size_t>(within.size(), 32));
  // The range must change the pick: some of the 32 best without it are farther.
  const bool dropped = std::ranges::any_of(std::span(unique).first(32), [&](const LightInstance& instance) {
    const auto& l = instance.light;
    return (l[0] * l[0]) + (l[1] * l[1]) + (l[2] * l[2]) > RANGE * RANGE;
  });
  bool in_range = count == expected && dropped;
  for (uint32_t rank = 0; in_range && rank < expected; ++rank) in_range = ranged[rank] == within[rank].light;
  std::printf("   range %.0f m: %u of %zu within it picked (of 100; some of the best 32 farther: %s), in order: %s\n", RANGE, count,
              within.size(), dropped ? "yes" : "no", in_range ? "yes" : "NO");
  CHECK(in_range);
}

// A battle's light draws: 128 draws of 40 instances (5120, the pick's first 4096 its candidates) at 9 to 60 m, every
// eighth draw again lights of the draw before it (the same light in two draws), and mirrored lights (x to -x: the same
// score at another place, kept apart). The draw before the repeats has a bright torch at 9 m (and its mirror): among the
// best, so the repeats reach the pick.
static std::vector<LightInstance> BattleLights() {
  std::mt19937 random(11);
  std::uniform_real_distribution<float> unit(0.f, 1.f);
  std::vector<LightInstance> lights;
  for (int draw = 0; draw < 128; ++draw) {
    for (int index = 0; index < 40; ++index) {
      if (draw % 8 == 7 && index < 20) {
        lights.push_back(lights[lights.size() - 40]);
        continue;
      }
      if (index % 10 == 9) {
        LightInstance mirrored = lights.back();
        mirrored.light[0] = -mirrored.light[0];
        lights.push_back(mirrored);
        continue;
      }
      const float distance = 9.f + (51.f * unit(random));
      lights.push_back({.colour = {0.5f + (4.f * unit(random)), 0.5f + unit(random), 0.2f, 0.f},
                        .light = {(unit(random) - 0.5f) * distance, -1.2f, distance, 2.f + (6.f * unit(random))}});
      if (draw % 8 == 6 && index == 8) {
        auto& torch = lights.back();
        torch = {.colour = {4.5f, 1.f, 0.2f, 0.f}, .light = {torch.light[0] * 9.f / distance, -1.2f, 9.f, 8.f}};
      }
    }
  }
  return lights;
}

// 9. The pick at a battle's light count: sss_lights since its repeat test reads a light's position only where the scores
// match against f6408aa1's (run.ps1's csos), on the same draws: the list, the dispatches, their coordinates and arguments to
// the bit, the lit bits within case 1's WARP variance (0.2%), and the pick the CPU's (repeats once, mirrored lights both).
// CONTROL: the CPU's pick without the repeat test must differ.
static void TestBattleLights(const std::string& before) {
  std::printf("9. Torch and Fire Shadows at a battle's light count\n");
  MakeDevice(D3D_DRIVER_TYPE_WARP);
  const std::array<Box, 1> boxes = {{{.low = {-0.5f, -2.f, 9.5f}, .high = {0.5f, -1.f, 10.5f}}}};
  const Frame frame = MakeFrame(480, 270, boxes, BOX_SUN);
  const auto battle = BattleLights();
  constexpr uint32_t WANTED = 32;
  auto& objects = sss::internal::runtime.objects;
  struct Outputs {
    std::vector<std::array<float, 4>> list;
    std::vector<std::array<int32_t, 4>> dispatches;
    std::vector<std::array<float, 4>> coordinates;
    std::vector<uint32_t> arguments;
    std::vector<uint32_t> mask;
  };
  const auto run = [&] {
    REQUIRE(LightFrames(frame, std::span<const LightInstance>(battle), WANTED, 0.f, 40));
    return Outputs{.list = ReadBuffer<std::array<float, 4>>(objects.lights.list.Get()),
                   .dispatches = ReadBuffer<std::array<int32_t, 4>>(objects.lights.dispatches.Get()),
                   .coordinates = ReadBuffer<std::array<float, 4>>(objects.lights.coordinates.Get()),
                   .arguments = ReadBuffer<uint32_t>(objects.lights.arguments.Get()),
                   .mask = ReadView<uint32_t>(objects.lights.mask_uav.Get())};
  };
  const Outputs now = run();
  const auto old_bytes = ReadFile(before + "\\sss_lights_f6408aa1.cso");
  REQUIRE(!old_bytes.empty());
  REQUIRE(SUCCEEDED(device->CreateComputeShader(old_bytes.data(), old_bytes.size(), nullptr, objects.lights.shader.ReleaseAndGetAddressOf())));
  const Outputs then = run();
  // Pixels Bend's waves write twice in one dispatch vary between runs on WARP (case 1): the bits at most 0.2% apart.
  size_t mask_apart = 0;
  for (size_t pixel = 0; pixel < now.mask.size(); ++pixel) mask_apart += (now.mask[pixel] != then.mask[pixel]) ? 1 : 0;
  const auto bytes_equal = [](const auto& a, const auto& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(a[0])) == 0;
  };
  const bool same = bytes_equal(now.list, then.list) && bytes_equal(now.dispatches, then.dispatches) && bytes_equal(now.coordinates, then.coordinates)
                    && bytes_equal(now.arguments, then.arguments);
  std::printf("   %zu instances in %zu draws: list, dispatches, coordinates, arguments %s f6408aa1's; lit bits %zu of %zu pixels apart\n",
              battle.size(), battle.size() / 40, same ? "the same as" : "NOT AS", mask_apart, now.mask.size());
  CHECK(same);
  CHECK(mask_apart * 500 < now.mask.size());
  // The CPU's pick of the first 4096: a light once (its first), the best WANTED by score, ties in order.
  const auto score = [](const LightInstance& instance) {
    const auto& l = instance.light;
    return std::max({instance.colour[0], instance.colour[1], instance.colour[2]}) * l[3] * l[3] / std::max((l[0] * l[0]) + (l[1] * l[1]) + (l[2] * l[2]), 1.f);
  };
  const auto pick = [&](bool once) {
    std::vector<LightInstance> candidates;
    for (size_t index = 0; index < 4096; ++index) {
      const auto& instance = battle[index];
      const bool repeat = once && std::any_of(battle.begin(), battle.begin() + static_cast<std::ptrdiff_t>(index), [&](const LightInstance& other) {
                            return score(other) == score(instance) && other.light == instance.light;
                          });
      if (!repeat) candidates.push_back(instance);
    }
    std::ranges::stable_sort(candidates, [&](const LightInstance& a, const LightInstance& b) { return score(a) > score(b); });
    candidates.resize(WANTED);
    return candidates;
  };
  const auto matches = [&](const std::vector<LightInstance>& expected) {
    uint32_t count = 0;
    std::memcpy(&count, &now.list[lights::internal::LIST_LIGHTS][0], sizeof(count));
    bool equal = count == WANTED;
    for (uint32_t rank = 0; equal && rank < WANTED; ++rank) {
      equal = now.list[rank] == expected[rank].light && now.list[lights::internal::LIST_LIGHTS + 1 + rank][0] == expected[rank].colour[0];
    }
    return equal;
  };
  const auto expected = pick(true);
  const auto mirrored = std::ranges::count_if(expected, [&](const LightInstance& a) {
    return std::ranges::any_of(expected, [&](const LightInstance& b) { return b.light[0] == -a.light[0] && b.light[0] != 0.f && b.light[2] == a.light[2]; });
  });
  const bool cpu = matches(expected);
  const bool control = matches(pick(false));
  std::printf("   the %u picked are the CPU's, in order: %s (%td of them mirrored pairs); CONTROL without the repeat test: %s\n", WANTED,
              cpu ? "yes" : "NO", mirrored, control ? "THE SAME (no repeat in the pick)" : "differs");
  CHECK(cpu && mirrored > 0);
  CHECK(!control);
  // X29: a torch on the camera's plane (view z 0, on the camera's x axis: clip w exactly 0): its Bend coordinate finite
  // (Bend's w == 0 guard, bend_sss_cpu.h). CONTROL (must fail): f6408aa1's sss_lights, without the guard, bound above.
  const LightInstance on_plane = {.colour = {4.f, 2.f, 0.5f, 0.f}, .light = {3.f, 0.f, 0.f, 6.f}};
  const auto plane_coordinate = [&] {
    REQUIRE(LightFrames(frame, std::span(&on_plane, 1), 4));
    return ReadBuffer<std::array<float, 4>>(objects.lights.coordinates.Get())[0];
  };
  const auto finite = [](const std::array<float, 4>& coordinate) { return std::ranges::all_of(coordinate, [](float value) { return std::isfinite(value); }); };
  const auto unguarded = plane_coordinate();
  REQUIRE(SUCCEEDED(device->CreateComputeShader(__sss_lights.data(), __sss_lights.size(), nullptr, objects.lights.shader.ReleaseAndGetAddressOf())));
  const auto guarded = plane_coordinate();
  std::printf("   a torch on the camera's plane: coordinate (%g, %g, %g, %g); CONTROL f6408aa1's (%g, %g, %g, %g)\n", guarded[0], guarded[1], guarded[2],
              guarded[3], unguarded[0], unguarded[1], unguarded[2], unguarded[3]);
  CHECK(finite(guarded));
  CHECK(!finite(unguarded));
}

static void TestReplay() {
  std::printf("6. the banners' replay\n");
  MakeDevice(D3D_DRIVER_TYPE_WARP);
  constexpr UINT W = 256;
  constexpr UINT H = 144;
  auto& runtime = sss::internal::runtime;
  const Frame frame = MakeFrame(W, H, BOX_SCENE, BOX_SUN);
  sss::Settings settings = Defaults();
  settings.thin_light = 0.35f;
  RunFrame(frame, settings);  // the module's objects
  ++runtime.frame;
  // The game: a G-buffer target, its depth (D32), a vertex shader placing quads by the camera, a pixel shader.
  const char* vertex_source = R"(
cbuffer camera : register(b0) { float4x4 view_projection; }
float4 main(float3 position : POSITION) : SV_Position { return mul(float4(position, 1), view_projection); })";
  const char* pixel_source = R"(float4 main(float4 position : SV_Position) : SV_Target { return float4(0.5, 0.25, 0.125, 1); })";
  const auto vertex_code = Compile(vertex_source, "vs_5_0");
  ComPtr<ID3D11VertexShader> vertex_shader;
  REQUIRE(SUCCEEDED(device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, vertex_shader.GetAddressOf())));
  const auto pixel_code = Compile(pixel_source, "ps_5_0");
  ComPtr<ID3D11PixelShader> pixel_shader;
  REQUIRE(SUCCEEDED(device->CreatePixelShader(pixel_code->GetBufferPointer(), pixel_code->GetBufferSize(), nullptr, pixel_shader.GetAddressOf())));
  const D3D11_INPUT_ELEMENT_DESC element = {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
  ComPtr<ID3D11InputLayout> layout;
  REQUIRE(SUCCEEDED(device->CreateInputLayout(&element, 1, vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), layout.GetAddressOf())));
  // A banner 2 x 2 m at 10 m, then a board in front of its left half.
  const std::array<float, 36> quads = {-1.f, -1.f, 10.f, -1.f, 1.f, 10.f, 1.f, 1.f, 10.f, -1.f, -1.f, 10.f, 1.f, 1.f, 10.f, 1.f, -1.f, 10.f,
                                       -3.f, -3.f, 8.f, -3.f, 3.f, 8.f, 0.f, 3.f, 8.f, -3.f, -3.f, 8.f, 0.f, 3.f, 8.f, 0.f, -3.f, 8.f};
  const auto vertices = MakeBuffer(sizeof(quads), D3D11_BIND_VERTEX_BUFFER, quads.data());
  std::array<float, 16> matrix = {};
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) matrix[(j * 4) + i] = frame.camera.view_projection[i][j];
  }
  const auto camera = MakeBuffer(sizeof(matrix), D3D11_BIND_CONSTANT_BUFFER, matrix.data());
  const Texture target = MakeTexture(W, H, DXGI_FORMAT_R8G8B8A8_UNORM, nullptr, 0, D3D11_BIND_RENDER_TARGET);
  const Texture kept = MakeTexture(W, H, DXGI_FORMAT_R8G8B8A8_UNORM, nullptr, 0, D3D11_BIND_RENDER_TARGET);
  ComPtr<ID3D11RenderTargetView> target_view;
  ComPtr<ID3D11RenderTargetView> kept_view;
  REQUIRE(SUCCEEDED(device->CreateRenderTargetView(target.texture.Get(), nullptr, target_view.GetAddressOf())));
  REQUIRE(SUCCEEDED(device->CreateRenderTargetView(kept.texture.Get(), nullptr, kept_view.GetAddressOf())));
  const D3D11_TEXTURE2D_DESC depth_desc = {W, H, 1, 1, DXGI_FORMAT_R32_TYPELESS, {1, 0}, D3D11_USAGE_DEFAULT,
                                           D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, 0, 0};
  ComPtr<ID3D11Texture2D> depth;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&depth_desc, nullptr, depth.GetAddressOf())));
  const D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc = {.Format = DXGI_FORMAT_D32_FLOAT, .ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D};
  ComPtr<ID3D11DepthStencilView> depth_view;
  REQUIRE(SUCCEEDED(device->CreateDepthStencilView(depth.Get(), &dsv_desc, depth_view.GetAddressOf())));
  const D3D11_DEPTH_STENCIL_DESC depth_state_desc = {.DepthEnable = TRUE, .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL,
                                                     .DepthFunc = D3D11_COMPARISON_LESS, .StencilEnable = TRUE, .StencilReadMask = 0xFF,
                                                     .StencilWriteMask = 0xFF,
                                                     .FrontFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS},
                                                     .BackFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS}};
  ComPtr<ID3D11DepthStencilState> depth_state;
  REQUIRE(SUCCEEDED(device->CreateDepthStencilState(&depth_state_desc, depth_state.GetAddressOf())));
  const D3D11_RASTERIZER_DESC rasterizer_desc = {.FillMode = D3D11_FILL_SOLID, .CullMode = D3D11_CULL_NONE, .DepthClipEnable = TRUE};
  ComPtr<ID3D11RasterizerState> rasterizer;
  REQUIRE(SUCCEEDED(device->CreateRasterizerState(&rasterizer_desc, rasterizer.GetAddressOf())));
  D3D11_BLEND_DESC blend_desc = {};
  blend_desc.RenderTarget[0] = {TRUE, D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, 0x7};
  ComPtr<ID3D11BlendState> blend;
  REQUIRE(SUCCEEDED(device->CreateBlendState(&blend_desc, blend.GetAddressOf())));
  context->ClearDepthStencilView(depth_view.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);
  std::array<ID3D11RenderTargetView*, 8> targets = {target_view.Get(), nullptr, nullptr, nullptr, nullptr, nullptr, kept_view.Get(), nullptr};
  context->OMSetRenderTargets(8, targets.data(), depth_view.Get());
  const D3D11_VIEWPORT viewport = {0.f, 0.f, static_cast<float>(W), static_cast<float>(H), 0.f, 1.f};
  context->RSSetViewports(1, &viewport);
  context->RSSetState(rasterizer.Get());
  const std::array<float, 4> blend_factor = {0.1f, 0.2f, 0.3f, 0.4f};
  context->OMSetBlendState(blend.Get(), blend_factor.data(), 0x00FF00FFu);
  context->OMSetDepthStencilState(depth_state.Get(), 7);
  context->IASetInputLayout(layout.Get());
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const UINT stride = 12;
  const UINT offset = 0;
  context->IASetVertexBuffers(0, 1, vertices.GetAddressOf(), &stride, &offset);
  context->VSSetShader(vertex_shader.Get(), nullptr, 0);
  context->VSSetConstantBuffers(0, 1, camera.GetAddressOf());
  context->PSSetShader(pixel_shader.Get(), nullptr, 0);

  mock::Device mock_device;
  mock_device.native = device;
  mock::CommandList commands;
  commands.owner = &mock_device;
  commands.native = context;
  static constexpr sss::ThinShader BANNER = {.hash = 0x12345678};
  DrawContext command;
  command.cmd_list = &commands;
  command.callback_data = &BANNER;
  command.arguments = {.vertex_count = 6, .instance_count = 1};
  context->Draw(6, 0);  // the game's draw
  const auto depth_drawn = ReadTexture<float>(depth.Get());
  const auto result = sss::OnThinDraw.template operator()<sss::ThinShader>(command);
  REQUIRE(result.finalize_callback != nullptr && result.post_callback == nullptr && !result.bypass);
  result.finalize_callback(command, nullptr);
  CHECK(runtime.thin_frame == runtime.frame);
  CHECK(depth_drawn == ReadTexture<float>(depth.Get()));
  // The game's state as it was.
  std::array<ID3D11RenderTargetView*, 8> bound_targets = {};
  ComPtr<ID3D11DepthStencilView> bound_depth;
  context->OMGetRenderTargets(8, bound_targets.data(), bound_depth.GetAddressOf());
  CHECK(bound_targets == targets);
  CHECK(bound_depth.Get() == depth_view.Get());
  for (auto* bound : bound_targets) {
    if (bound != nullptr) bound->Release();
  }
  ComPtr<ID3D11PixelShader> bound_pixel_shader;
  context->PSGetShader(bound_pixel_shader.GetAddressOf(), nullptr, nullptr);
  CHECK(bound_pixel_shader.Get() == pixel_shader.Get());
  ComPtr<ID3D11RasterizerState> bound_rasterizer;
  context->RSGetState(bound_rasterizer.GetAddressOf());
  CHECK(bound_rasterizer.Get() == rasterizer.Get());
  ComPtr<ID3D11DepthStencilState> bound_depth_state;
  UINT stencil_reference = 0;
  context->OMGetDepthStencilState(bound_depth_state.GetAddressOf(), &stencil_reference);
  CHECK(bound_depth_state.Get() == depth_state.Get() && stencil_reference == 7);
  ComPtr<ID3D11BlendState> bound_blend;
  std::array<float, 4> bound_factor = {};
  UINT sample_mask = 0;
  context->OMGetBlendState(bound_blend.GetAddressOf(), bound_factor.data(), &sample_mask);
  CHECK(bound_blend.Get() == blend.Get() && bound_factor == blend_factor && sample_mask == 0x00FF00FFu);

  context->Draw(6, 6);  // a later draw: the board
  const auto scene_depth = ReadTexture<float>(depth.Get());
  const auto thin = ReadView<float>(runtime.objects.thin_srv.Get());
  int tagged = 0;
  int covered = 0;
  int covered_tagged = 0;
  int elsewhere_wrong = 0;
  const auto matches = [](float thin_depth, float depth_value) {
    const float remaining = std::fabs(1.f - depth_value);
    return remaining > 1e-7f && std::fabs(thin_depth - depth_value) <= 4e-7f + (2e-4f * remaining);
  };
  for (size_t pixel = 0; pixel < thin.size(); ++pixel) {
    const bool banner = depth_drawn[pixel] < 1.f;
    const bool board = scene_depth[pixel] < depth_drawn[pixel] || (!banner && scene_depth[pixel] < 1.f);
    if (banner && !board) {
      tagged += matches(thin[pixel], scene_depth[pixel]) ? 1 : 0;
    } else if (banner) {
      ++covered;
      covered_tagged += matches(thin[pixel], scene_depth[pixel]) ? 1 : 0;
    } else if (thin[pixel] != 1.f) {
      ++elsewhere_wrong;
    }
  }
  const auto visible = std::ranges::count_if(std::views::iota(size_t{0}, thin.size()), [&](size_t pixel) {
    return depth_drawn[pixel] < 1.f && !(scene_depth[pixel] < depth_drawn[pixel]);
  });
  std::printf("   banner pixels in view %zu: %d tagged; covered later %d: %d tagged; elsewhere not far: %d\n", static_cast<size_t>(visible), tagged,
              covered, covered_tagged, elsewhere_wrong);
  CHECK(visible > 100 && tagged == visible);
  CHECK(covered > 100 && covered_tagged == 0);
  CHECK(elsewhere_wrong == 0);
}

static void TestRowsTurnedOff() {
  std::printf("7. rows turned off\n");
  MakeDevice(D3D_DRIVER_TYPE_WARP);
  auto& runtime = sss::internal::runtime;
  auto& objects = runtime.objects;
  const std::array<Box, 2> boxes = {{BOX_SCENE[0], {.low = {-1.f, -0.6f, 12.f}, .high = {1.f, 1.4f, 12.05f}, .kind = Kind::THIN, .albedo = {0.8f, 0.1f, 0.1f}}}};
  const Frame frame = MakeFrame(480, 270, boxes, BOX_SUN);
  const LightInstance torch = {.colour = {4.f, 2.f, 0.5f, 0.f}, .light = {2.f, -1.2f, 10.f, 6.f}};
  const auto buffer = MakeBuffer(sizeof(torch), D3D11_BIND_VERTEX_BUFFER, &torch);
  const lights::Instances drawn = {.buffer = buffer.Get(), .first = 0, .count = 1, .stride = sizeof(LightInstance),
                                   .position = offsetof(LightInstance, light), .colour = offsetof(LightInstance, colour)};
  sss::Settings all = Defaults();
  all.thin_light = 0.35f;
  all.coloured = true;
  all.sun_disk = 0.5f;
  all.lights = 4;
  // Two frames (the second marches the first's light), the lights taken off after each light draw (rt_scene's
  // UnbindLocalLights).
  const auto run_all = [&] {
    for (int repeat = 0; repeat < 2; ++repeat) {
      RunFrame(frame, all, true);
      lights::OnLightDraw(context.Get(), drawn);
      ID3D11ShaderResourceView* none = nullptr;
      context->PSSetShaderResources(lights::internal::LIGHT_MASK_SLOT, 1, &none);
    }
  };
  run_all();
  REQUIRE(objects.tint_srv && objects.penumbra_srv && objects.lights.shader && objects.lights.mask_srv && runtime.tint_frame == runtime.frame);
  // Their resources held here too: this the one reference left once the module lets them go.
  const auto resource = [](ID3D11View* view) {
    ComPtr<ID3D11Resource> held;
    view->GetResource(held.GetAddressOf());
    return held;
  };
  const auto only_here = [](IUnknown* object) {
    object->AddRef();
    return object->Release() == 1;
  };
  const ComPtr<ID3D11Resource> tint = resource(objects.tint_srv.Get());
  const ComPtr<ID3D11Resource> penumbra = resource(objects.penumbra_srv.Get());
  const ComPtr<ID3D11Resource> mask = resource(objects.lights.mask_srv.Get());
  const ComPtr<ID3D11Buffer> instances = objects.lights.instances[0];
  // The filter a sun draw left at t50.
  context->PSSetShaderResources(sss::internal::TINT_SLOT, 1, objects.tint_srv.GetAddressOf());
  runtime.bound_tint = objects.tint_srv;

  mock::Device mock_device;
  mock_device.native = device;
  mock_device.back_buffer.type = reshade::api::resource_type::texture_2d;
  mock_device.back_buffer.texture.width = 480;
  mock_device.back_buffer.texture.height = 270;
  mock::Swapchain swapchain;
  swapchain.owner = &mock_device;
  mock::CommandList commands;
  commands.owner = &mock_device;
  commands.native = context;
  sss::Settings settings = all;
  settings.coloured = false;
  sss::Update(&swapchain, settings);
  CHECK(!objects.tint_srv && !objects.tint_uav && objects.thin_srv && objects.penumbra_srv && objects.lights.shader);
  // The next sun draw (of a shader not patched: no occlusion) takes the filter off.
  static const twattila::light_bounce::SunDraw SUN = {.shader_hash = 0x87654321u};
  DrawContext command;
  command.cmd_list = &commands;
  command.callback_data = &SUN;
  sss::OnSunDraw.template operator()<twattila::light_bounce::SunDraw>(command);
  ComPtr<ID3D11ShaderResourceView> bound;
  context->PSGetShaderResources(sss::internal::TINT_SLOT, 1, bound.GetAddressOf());
  const bool tint_freed = !bound && !runtime.bound_tint && only_here(tint.Get());
  settings.sun_disk = 0.f;
  sss::Update(&swapchain, settings);
  const bool penumbra_freed = !objects.penumbra_uav && !objects.penumbra_srv && only_here(penumbra.Get());
  CHECK(objects.thin_srv && objects.lights.shader);
  settings.lights = 0;
  sss::Update(&swapchain, settings);
  const bool lights_freed = !objects.lights.shader && !objects.lights.camera && !objects.lights.mask_uav && !objects.lights.mask_srv
                            && objects.lights.records[0].empty() && objects.lights.records[1].empty() && only_here(mask.Get())
                            && only_here(instances.Get());
  CHECK(objects.thin_srv);
  settings.thin_light = 0.f;
  sss::Update(&swapchain, settings);
  const bool banners_freed = !objects.thin_texture && !objects.thin_rtv && !objects.thin_srv && !objects.thin_depth_state && objects.thin_rasterizers.empty();
  std::printf("   let go: Coloured Shadows' filter %s (t50 %s), the penumbra %s, the lights' %s, the banners' depth %s\n", tint_freed ? "yes" : "NO",
              bound ? "still bound" : "free", penumbra_freed ? "yes" : "NO", lights_freed ? "yes" : "NO", banners_freed ? "yes" : "NO");
  CHECK(tint_freed && penumbra_freed && lights_freed && banners_freed);
  // On again: made again.
  run_all();
  CHECK(objects.tint_srv && objects.penumbra_srv && objects.lights.shader && objects.lights.mask_srv && runtime.tint_frame == runtime.frame
        && runtime.soft_frame == runtime.frame);
}

// Shogun 2's sun shadow passes' inputs for `frame` (sss.hlsli): camera_VS_PS, shadowmap_PS over 4 cascades (random depths
// around the ground's and the box's, or all lit), the comparison and border samplers, Bend's sss constants for each dispatch
// sss_setup lists, shader_injection (all 0: the rows' defaults; or one row at a value).
struct ShogunInputs {
  ComPtr<ID3D11Buffer> camera;
  ComPtr<ID3D11Buffer> shadow_map;
  ComPtr<ID3D11Texture2D> map;
  ComPtr<ID3D11ShaderResourceView> map_srv;
  ComPtr<ID3D11SamplerState> comparison;
  ComPtr<ID3D11SamplerState> border;
  std::vector<ComPtr<ID3D11Buffer>> bend_buffers;
  std::vector<std::array<uint32_t, 3>> bend_groups;
};

static ComPtr<ID3D11Buffer> ShogunInjection(size_t offset, float value) {
  std::vector<float> floats((sizeof(shogun2::ShaderInjectData) + 15) / 16 * 4, 0.f);
  floats[offset / sizeof(float)] = value;
  return MakeBuffer(static_cast<UINT>(floats.size() * sizeof(float)), D3D11_BIND_CONSTANT_BUFFER, floats.data());
}

static ShogunInputs MakeShogunInputs(const Frame& frame, bool lit_cascades) {
  ShogunInputs inputs;
  const UINT width = frame.camera.width;
  const UINT height = frame.camera.height;
  // The sun's dispatches as sss_setup lists them for the frame: the light coordinate, 8 dispatches' offsets (x the wave size)
  // and wave counts.
  RunFrame(frame, Defaults());
  auto& objects = sss::internal::runtime.objects;
  const auto parameters = ReadBuffer<std::array<int32_t, 4>>(objects.parameters.Get());
  const auto arguments = ReadBuffer<std::array<uint32_t, 3>>(objects.arguments.Get());
  // camera_VS_PS: view_projection c10, inv_view_projection c30, screen_size c37, g_viewport_dimensions c38,
  // g_render_target_dimensions c40 (each register a matrix's column, as MakeFrame's).
  const auto put = [](std::span<float> constants, UINT base, const Matrix& m) {
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j) constants[((base + j) * 4) + i] = m[i][j];
    }
  };
  std::array<float, 44 * 4> camera = {};
  put(camera, 10, frame.camera.view_projection);
  put(camera, 30, Inverse(frame.camera.view_projection));
  for (const UINT at : {37u, 38u, 40u}) {
    camera[at * 4] = static_cast<float>(width);
    camera[(at * 4) + 1] = static_cast<float>(height);
    camera[(at * 4) + 2] = 1.f / static_cast<float>(width);
    camera[(at * 4) + 3] = 1.f / static_cast<float>(height);
  }
  // shadowmap_PS: 4 splits of 128 texels, each a top-down map over twice the last's width; fade ranges, bias, the sun, the
  // splits' distances.
  constexpr UINT MAP_SIZE = 128;
  std::array<float, 24 * 4> shadow_map = {MAP_SIZE, MAP_SIZE, 1.f / MAP_SIZE, 1.f / MAP_SIZE};
  for (UINT split = 0; split < 4; ++split) {
    const float scale = 1.f / (8.f * static_cast<float>(1u << split));
    put(shadow_map, 1 + (split * 4), {{{scale, 0.f, 0.f, 0.f}, {0.f, 0.f, -0.01f, 0.f}, {0.f, scale, 0.f, 0.f}, {0.5f, 0.5f - (10.f * scale), 0.5f, 1.f}}});
  }
  std::copy_n(std::array<float, 4>{0.1f, 0.1f, 0.01f, 4.f}.begin(), 4, shadow_map.begin() + (17 * 4));
  std::copy(frame.sun.begin(), frame.sun.end(), shadow_map.begin() + (18 * 4));
  for (UINT split = 0; split < 5; ++split) shadow_map[(19 + split) * 4] = std::array<float, 5>{0.f, 4.f, 8.f, 16.f, 60.f}[split];
  inputs.camera = MakeBuffer(sizeof(camera), D3D11_BIND_CONSTANT_BUFFER, camera.data());
  inputs.shadow_map = MakeBuffer(sizeof(shadow_map), D3D11_BIND_CONSTANT_BUFFER, shadow_map.data());
  // The cascades' depths: random around the ground's and the box's, or the far plane (nothing in them shadowed).
  std::mt19937 random(8);
  std::uniform_real_distribution<float> map_depth(0.47f, 0.53f);
  std::vector<float> map_texels(MAP_SIZE * MAP_SIZE * 4);
  for (auto& texel : map_texels) texel = lit_cascades ? 1.f : map_depth(random);
  const D3D11_TEXTURE2D_DESC map_desc = {MAP_SIZE, MAP_SIZE, 1, 4, DXGI_FORMAT_R32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
  std::array<D3D11_SUBRESOURCE_DATA, 4> map_slices = {};
  for (UINT slice = 0; slice < 4; ++slice) map_slices[slice] = {map_texels.data() + (slice * MAP_SIZE * MAP_SIZE), MAP_SIZE * 4, 0};
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&map_desc, map_slices.data(), inputs.map.GetAddressOf())));
  REQUIRE(SUCCEEDED(device->CreateShaderResourceView(inputs.map.Get(), nullptr, inputs.map_srv.GetAddressOf())));
  const D3D11_SAMPLER_DESC comparison_desc = {D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP,
                                        D3D11_TEXTURE_ADDRESS_CLAMP, 0.f, 1, D3D11_COMPARISON_LESS_EQUAL, {}, 0.f, D3D11_FLOAT32_MAX};
  const D3D11_SAMPLER_DESC border_desc = {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_BORDER, D3D11_TEXTURE_ADDRESS_BORDER,
                                         D3D11_TEXTURE_ADDRESS_BORDER, 0.f, 1, D3D11_COMPARISON_NEVER, {1.f, 1.f, 1.f, 1.f}, 0.f, D3D11_FLOAT32_MAX};
  REQUIRE(SUCCEEDED(device->CreateSamplerState(&comparison_desc, inputs.comparison.GetAddressOf())));
  REQUIRE(SUCCEEDED(device->CreateSamplerState(&border_desc, inputs.border.GetAddressOf())));
  // Bend's sss constants for each listed dispatch: the game's thickness, edge threshold and contrast, the light coordinate and
  // the dispatch's offset.
  for (size_t dispatch = 0; dispatch < 8; ++dispatch) {
    if (arguments[dispatch][0] == 0 || arguments[dispatch][1] == 0 || arguments[dispatch][2] == 0) continue;
    std::array<uint32_t, 12> bend = {std::bit_cast<uint32_t>(0.005f), std::bit_cast<uint32_t>(0.02f), std::bit_cast<uint32_t>(4.f), 0u};
    for (int axis = 0; axis < 4; ++axis) bend[4 + axis] = static_cast<uint32_t>(parameters[0][axis]);
    bend[8] = static_cast<uint32_t>(parameters[1 + dispatch][0]);
    bend[9] = static_cast<uint32_t>(parameters[1 + dispatch][1]);
    inputs.bend_buffers.push_back(MakeBuffer(sizeof(bend), D3D11_BIND_CONSTANT_BUFFER, bend.data()));
    inputs.bend_groups.push_back(arguments[dispatch]);
  }
  REQUIRE(!inputs.bend_buffers.empty());
  return inputs;
}

// One of Shogun 2's sun shadow passes (`shader`; with the cascades' layout or without, Bend's dispatches or sss02's) on `frame`
// into `output` (R8, cleared first), with `settings` at b13; the extended passes' banners' depth at t5, colour at t6 and filter
// at u2 where given. The mask after it.
struct ShogunExtras {
  ID3D11ShaderResourceView* thin = nullptr;
  ID3D11ShaderResourceView* colour = nullptr;
  ID3D11UnorderedAccessView* tint = nullptr;
};
static std::vector<uint8_t> RunShogunPass(const Frame& frame, const ShogunInputs& inputs, const Texture& output, ID3D11UnorderedAccessView* output_uav,
                                          ID3D11ComputeShader* shader, bool cascades, bool bend, ID3D11Buffer* settings, const ShogunExtras& extras = {}) {
  constexpr std::array<UINT, 4> CLEAR = {};
  context->ClearUnorderedAccessViewUint(output_uav, CLEAR.data());
  context->CSSetShader(shader, nullptr, 0);
  context->CSSetConstantBuffers(13, 1, &settings);
  context->CSSetConstantBuffers(0, 1, inputs.camera.GetAddressOf());
  context->CSSetUnorderedAccessViews(0, 1, &output_uav, nullptr);
  if (cascades) {
    context->CSSetConstantBuffers(1, 1, inputs.shadow_map.GetAddressOf());
    const std::array<ID3D11ShaderResourceView*, 4> views = {inputs.map_srv.Get(), frame.colour.srv.Get(), frame.depth.srv.Get(), frame.depth.srv.Get()};
    context->CSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
    const std::array<ID3D11SamplerState*, 2> samplers = {inputs.comparison.Get(), inputs.border.Get()};
    context->CSSetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());
  } else {
    context->CSSetShaderResources(0, 1, frame.depth.srv.GetAddressOf());
    context->CSSetSamplers(0, 1, inputs.border.GetAddressOf());
  }
  const std::array<ID3D11ShaderResourceView*, 2> extra_views = {extras.thin, extras.colour};
  context->CSSetShaderResources(5, static_cast<UINT>(extra_views.size()), extra_views.data());
  context->CSSetUnorderedAccessViews(2, 1, &extras.tint, nullptr);
  if (bend) {
    for (size_t dispatch = 0; dispatch < inputs.bend_buffers.size(); ++dispatch) {
      context->CSSetConstantBuffers(cascades ? 2 : 1, 1, inputs.bend_buffers[dispatch].GetAddressOf());
      context->Dispatch(inputs.bend_groups[dispatch][0], inputs.bend_groups[dispatch][1], inputs.bend_groups[dispatch][2]);
    }
  } else {
    context->Dispatch((frame.camera.width + 31) / 32, (frame.camera.height + 31) / 32, 1);
  }
  context->ClearState();
  return ReadTexture<uint8_t>(output.texture.Get());
}

static ComPtr<ID3D11ComputeShader> LoadCso(const std::string& path) {
  const auto code = ReadFile(path);
  REQUIRE(!code.empty());
  ComPtr<ID3D11ComputeShader> shader;
  REQUIRE(SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, shader.GetAddressOf())));
  return shader;
}

static void TestShogun2(const std::string& before_folder) {
  std::printf("8. Shogun 2's sss00-02 at the rows' defaults\n");
  MakeDevice(D3D_DRIVER_TYPE_WARP);
  const Frame frame = MakeFrame(480, 270, BOX_SCENE, BOX_SUN);
  const ShogunInputs inputs = MakeShogunInputs(frame, false);
  const Texture output = MakeTexture(frame.camera.width, frame.camera.height, DXGI_FORMAT_R8_UNORM, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS);
  ComPtr<ID3D11UnorderedAccessView> output_uav;
  REQUIRE(SUCCEEDED(device->CreateUnorderedAccessView(output.texture.Get(), nullptr, output_uav.GetAddressOf())));
  const auto defaults = ShogunInjection(0, 0.f);
  struct Pass {
    const char* name;
    bool cascades;
    bool bend;
    size_t control_row;
  };
  constexpr std::array<Pass, 3> PASSES = {{
      {.name = "sss00_0x34ECD06E", .cascades = true, .bend = true, .control_row = offsetof(shogun2::ShaderInjectData, sss_contrast)},
      {.name = "sss01_0x1B529672", .cascades = false, .bend = true, .control_row = offsetof(shogun2::ShaderInjectData, sss_contrast)},
      {.name = "sss02_0x78E88E1D", .cascades = true, .bend = false, .control_row = offsetof(shogun2::ShaderInjectData, shadow_map_stability)},
  }};
  const std::filesystem::path after_folder = std::filesystem::path(before_folder).parent_path() / "after_cso";
  for (const auto& pass : PASSES) {
    const auto before = LoadCso(before_folder + "\\" + pass.name + ".cso");
    const auto after = LoadCso((after_folder / (std::string(pass.name) + ".cso")).string());
    const auto control = ShogunInjection(pass.control_row, 2.f);
    std::vector<std::vector<uint8_t>> ours;
    std::vector<std::vector<uint8_t>> theirs;
    std::vector<std::vector<uint8_t>> controls;
    for (int round = 0; round < RUNS; ++round) {
      ours.push_back(RunShogunPass(frame, inputs, output, output_uav.Get(), after.Get(), pass.cascades, pass.bend, defaults.Get()));
      theirs.push_back(RunShogunPass(frame, inputs, output, output_uav.Get(), before.Get(), pass.cascades, pass.bend, defaults.Get()));
      controls.push_back(RunShogunPass(frame, inputs, output, output_uav.Get(), after.Get(), pass.cascades, pass.bend, control.Get()));
    }
    CompareRuns((std::string(pass.name) + " at the rows' defaults against fc24dbce's").c_str(), ours, theirs);
    CompareRuns((std::string("CONTROL ") + pass.name + " with " + (pass.bend ? "Bend's contrast" : "shadow map stability") + " at 2").c_str(),
                controls, theirs, false);
  }
}

// 10. Shogun 2's Light Through Banners and Coloured Shadows: its extended passes (sss_distance / sss_distance_cascades, which its
//    screen_space_shadows.hpp swaps in; from this tree, run.ps1's after_cso) on case 3's red banner with its depth at t5 and the
//    G-buffer's colour (linear, as Shogun's sun draws read it) at t6, the cascades lit: the banner's shadow keeps Light Through
//    Banners (0.35) of the sun (Bend's occlusion its solid one's x 0.65, within 0.12), the filter at u2 the banner's colour over
//    its brightest channel (red kept), none in the sun; the cascades' version the same within 0.01. With the rows off the
//    extended pass is sss01's (the game's) to the bit. CONTROLS (must fail): the banners' depth unbound, its shadow solid; the
//    filter's target unbound, no filter written.
static void TestShogun2Banners(const std::string& before_folder) {
  std::printf("10. Shogun 2's Light Through Banners, Coloured Shadows\n");
  MakeDevice(D3D_DRIVER_TYPE_WARP);
  const Vector sun = {0.2f, -0.6f, -0.77f};
  const std::array<Box, 1> boxes = {{{.low = {-1.f, -0.6f, 12.f}, .high = {1.f, 1.4f, 12.05f}, .kind = Kind::THIN, .albedo = {0.8f, 0.1f, 0.1f}}}};
  Frame frame = MakeFrame(480, 270, boxes, sun);
  const ShogunInputs inputs = MakeShogunInputs(frame, true);
  const UINT w = frame.camera.width;
  const UINT h = frame.camera.height;
  // Shogun's G-buffer colour as the sun draws read it: linear.
  std::vector<uint32_t> linear(w * h);
  for (size_t pixel = 0; pixel < linear.size(); ++pixel) {
    const int hit = frame.picture.hit[pixel];
    const Vector albedo = (hit >= 0) ? boxes[hit].albedo : Vector{0.5f, 0.5f, 0.5f};
    uint32_t packed = 0xFF000000u;
    for (int channel = 0; channel < 3; ++channel) packed |= static_cast<uint32_t>(std::lround(albedo[channel] * 255.f)) << (8 * channel);
    linear[pixel] = packed;
  }
  frame.colour = MakeTexture(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, linear.data(), w * 4);
  const Texture output = MakeTexture(w, h, DXGI_FORMAT_R8_UNORM, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS);
  const Texture tint = MakeTexture(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS);
  ComPtr<ID3D11UnorderedAccessView> output_uav;
  ComPtr<ID3D11UnorderedAccessView> tint_uav;
  REQUIRE(SUCCEEDED(device->CreateUnorderedAccessView(output.texture.Get(), nullptr, output_uav.GetAddressOf())));
  REQUIRE(SUCCEEDED(device->CreateUnorderedAccessView(tint.texture.Get(), nullptr, tint_uav.GetAddressOf())));
  const std::filesystem::path after_folder = std::filesystem::path(before_folder).parent_path() / "after_cso";
  const auto plain = LoadCso((after_folder / "sss01_0x1B529672.cso").string());
  const auto extended = LoadCso((after_folder / "sss_distance.cso").string());
  const auto extended_cascades = LoadCso((after_folder / "sss_distance_cascades.cso").string());
  const auto defaults = ShogunInjection(0, 0.f);
  const auto banners = ShogunInjection(offsetof(shogun2::ShaderInjectData, sss_banner_light), 0.35f);
  const ShogunExtras coloured = {.thin = frame.thin.srv.Get(), .colour = frame.colour.srv.Get(), .tint = tint_uav.Get()};
  const auto clear_tint = [&] {
    constexpr std::array<UINT, 4> CLEAR = {};
    context->ClearUnorderedAccessViewUint(tint_uav.Get(), CLEAR.data());
  };

  // The rows off: the extended pass against the game's.
  std::vector<std::vector<uint8_t>> ours;
  std::vector<std::vector<uint8_t>> theirs;
  for (int round = 0; round < RUNS; ++round) {
    ours.push_back(RunShogunPass(frame, inputs, output, output_uav.Get(), extended.Get(), false, true, defaults.Get()));
    theirs.push_back(RunShogunPass(frame, inputs, output, output_uav.Get(), plain.Get(), false, true, defaults.Get()));
  }
  CompareRuns("sss_distance with the rows off against sss01", ours, theirs);

  // The banner's shadow on the ground, away from its edges (3 pixels in), and the ground in the sun.
  const Vector to_sun = {-frame.sun[0], -frame.sun[1], -frame.sun[2]};
  std::vector<uint8_t> shadow(w * h, 0);
  for (size_t pixel = 0; pixel < shadow.size(); ++pixel) {
    float distance = 0.f;
    if (frame.picture.hit[pixel] == -1 && Occluder(boxes, frame.picture.position[pixel], to_sun, 1e30f, &distance) >= 0) shadow[pixel] = 1;
  }
  struct Means {
    double occlusion = 0.0;
    std::array<double, 3> filter = {};
    int inner = 0;
    uint32_t sunlit_tint = 0;
  };
  // Shogun's mask is the shadow term (1 lit): the occlusion 1 - it.
  const auto measure = [&](const std::vector<uint8_t>& mask, const std::vector<uint32_t>& filter) {
    Means means;
    for (UINT y = 3; y + 3 < h; ++y) {
      for (UINT x = 3; x + 3 < w; ++x) {
        bool all = true;
        bool none = true;
        for (int dy = -3; dy <= 3; ++dy) {
          for (int dx = -3; dx <= 3; ++dx) {
            const uint8_t value = shadow[((y + dy) * w) + x + dx];
            all = all && value != 0;
            none = none && value == 0;
          }
        }
        const size_t pixel = (y * w) + x;
        if (all) {
          means.occlusion += 1.0 - (mask[pixel] / 255.0);
          for (int channel = 0; channel < 3; ++channel) means.filter[channel] += ((filter[pixel] >> (8 * channel)) & 0xFF) / 255.0;
          ++means.inner;
        } else if (none && frame.picture.hit[pixel] == -1) {
          means.sunlit_tint = std::max(means.sunlit_tint, filter[pixel] & 0xFFFFFFu);
        }
      }
    }
    REQUIRE(means.inner > 50);
    means.occlusion /= means.inner;
    for (double& channel : means.filter) channel /= means.inner;
    return means;
  };
  const auto pass = [&](ID3D11ComputeShader* shader, bool cascades, ID3D11Buffer* settings, const ShogunExtras& extras) {
    clear_tint();
    const auto mask = RunShogunPass(frame, inputs, output, output_uav.Get(), shader, cascades, true, settings, extras);
    return measure(mask, ReadTexture<uint32_t>(tint.texture.Get()));
  };
  const Means solid = pass(extended.Get(), false, defaults.Get(), {});
  const Means banner = pass(extended.Get(), false, banners.Get(), coloured);
  const Means banner_cascades = pass(extended_cascades.Get(), true, banners.Get(), coloured);
  const Means unbound = pass(extended.Get(), false, banners.Get(), {.colour = coloured.colour, .tint = coloured.tint});
  const Means no_target = pass(extended.Get(), false, banners.Get(), {.thin = coloured.thin, .colour = coloured.colour});
  std::printf("   %d shadow pixels: occlusion %.3f (solid %.3f), filter %.3f %.3f %.3f (expected 0 0.875 0.875), in the sun %06X; with the "
              "cascades %.3f, %.3f %.3f %.3f; CONTROLS the banners' depth unbound %.3f, the filter's target unbound %.3f %.3f %.3f\n",
              banner.inner, banner.occlusion, solid.occlusion, banner.filter[0], banner.filter[1], banner.filter[2], banner.sunlit_tint,
              banner_cascades.occlusion, banner_cascades.filter[0], banner_cascades.filter[1], banner_cascades.filter[2], unbound.occlusion,
              no_target.filter[0], no_target.filter[1], no_target.filter[2]);
  CHECK(solid.occlusion > 0.3);
  CHECK(std::fabs((banner.occlusion / solid.occlusion) - 0.65) < 0.12);
  CHECK(banner.filter[0] < 0.1 && banner.filter[1] > 0.6 && banner.filter[2] > 0.6);
  CHECK(banner.sunlit_tint == 0);
  CHECK(std::fabs(banner_cascades.occlusion - banner.occlusion) < 0.01);
  for (int channel = 0; channel < 3; ++channel) CHECK(std::fabs(banner_cascades.filter[channel] - banner.filter[channel]) < 0.01);
  CHECK(std::fabs((unbound.occlusion / solid.occlusion) - 1.0) < 0.05);
  CHECK(no_target.filter[1] == 0.0 && no_target.filter[2] == 0.0);
}

// ---- --time -----------------------------------------------------------------------------------------------------------

// The median GPU time (ms) of `work` over 30 frames, after 5 to warm up.
template <typename Work>
static double GpuMilliseconds(Work work) {
  ComPtr<ID3D11Query> disjoint;
  ComPtr<ID3D11Query> start;
  ComPtr<ID3D11Query> end;
  const D3D11_QUERY_DESC disjoint_desc = {D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
  const D3D11_QUERY_DESC stamp_desc = {D3D11_QUERY_TIMESTAMP, 0};
  REQUIRE(SUCCEEDED(device->CreateQuery(&disjoint_desc, disjoint.GetAddressOf())));
  REQUIRE(SUCCEEDED(device->CreateQuery(&stamp_desc, start.GetAddressOf())));
  REQUIRE(SUCCEEDED(device->CreateQuery(&stamp_desc, end.GetAddressOf())));
  std::vector<double> times;
  for (int frame = 0; frame < 35; ++frame) {
    context->Begin(disjoint.Get());
    context->End(start.Get());
    work();
    context->End(end.Get());
    context->End(disjoint.Get());
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT frequency = {};
    while (context->GetData(disjoint.Get(), &frequency, sizeof(frequency), 0) != S_OK) {
    }
    UINT64 begin_time = 0;
    UINT64 end_time = 0;
    while (context->GetData(start.Get(), &begin_time, sizeof(begin_time), 0) != S_OK) {
    }
    while (context->GetData(end.Get(), &end_time, sizeof(end_time), 0) != S_OK) {
    }
    if (frame >= 5 && !frequency.Disjoint) times.push_back(static_cast<double>(end_time - begin_time) * 1000.0 / static_cast<double>(frequency.Frequency));
  }
  std::ranges::sort(times);
  return times.empty() ? -1.0 : times[times.size() / 2];
}

static void Time(const std::string& before) {
  std::printf("--time: the hardware GPU at 3840 x 2160\n");
  MakeDevice(D3D_DRIVER_TYPE_HARDWARE);
  const std::array<Box, 4> boxes = {{
      {.low = {-2.f, -2.f, 14.f}, .high = {2.f, 1.f, 18.f}},
      {.low = {-6.f, -2.f, 20.f}, .high = {6.f, 0.f, 22.f}},
      {.low = {3.f, -2.f, 10.f}, .high = {3.2f, 2.f, 10.2f}},
      {.low = {-4.f, -0.6f, 11.f}, .high = {-2.f, 1.4f, 11.05f}, .kind = Kind::THIN, .albedo = {0.8f, 0.1f, 0.1f}},
  }};
  const Frame frame = MakeFrame(3840, 2160, boxes, {0.25f, -0.55f, -0.8f});
  sss::internal::runtime.albedo_power = 2.f;
  struct Row {
    const char* name;
    sss::Settings settings;
    bool thin;
  };
  sss::Settings base = Defaults();
  sss::Settings auto_thickness = base;
  auto_thickness.auto_thickness = true;
  sss::Settings banners = base;
  banners.thin_light = 0.35f;
  sss::Settings coloured = banners;
  coloured.coloured = true;
  sss::Settings softer = base;
  softer.sun_disk = 0.5f;
  sss::Settings softness = base;
  softness.softness = 2.f;
  const std::array<Row, 6> rows = {{{"rows off", base, false},
                                    {"Softness 2 px (today's blur)", softness, false},
                                    {"Auto Thickness", auto_thickness, false},
                                    {"Light Through Banners", banners, true},
                                    {"+ Coloured Shadows", coloured, true},
                                    {"Softer Away from the Caster 0.5", softer, false}}};
  double off = 0.0;
  for (const auto& row : rows) {
    const double milliseconds = GpuMilliseconds([&] { RunFrame(frame, row.settings, row.thin); });
    if (&row == &rows[0]) off = milliseconds;
    std::printf("   %-34s %.3f ms (%+.3f)\n", row.name, milliseconds, milliseconds - off);
  }
  // O-SS5 (G1, plan_1117 designs\sss_scale_thickness.md): sss_bend below 100% render scale, the render's depth in the
  // top-left of the 3840 x 2160 screen's (as the DLSS addon draws it), A' (screen steps, the add-on's) against C (the form
  // before O-SS5: bc423ef9d's sss_bend a sample a render pixel, samples x share, thickness / share), plain and with
  // Softer Away from the Caster (the extended find variant). At 100% both are Bend's (the strict gate: within 1%).
  {
    const auto load = [&](const char* name) {
      const auto code = ReadFile(before + "\\" + name + ".cso");
      REQUIRE(!code.empty());
      ComPtr<ID3D11ComputeShader> shader;
      REQUIRE(SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, shader.GetAddressOf())));
      return shader;
    };
    const ComPtr<ID3D11ComputeShader> today = load("sss_bend_bc423ef9d");
    const ComPtr<ID3D11ComputeShader> today_find = load("sss_bend_extended_find_bc423ef9d");
    // apd_ O2 (designs\sss_aprime_cheaper.md): the module's steps shaders are O2 (one shared screen grid); today's A'
    // (5c2c5042f, every lane its own samples) a third column. A fourth, numbers for ATN where the folder holds them
    // (o2_'s G1 job compiles them from a scratch copy): O2 with 2304 groupshared slots (18 KB).
    const ComPtr<ID3D11ComputeShader> aprime = load("sss_bend_steps_5c2c5042f");
    const ComPtr<ID3D11ComputeShader> aprime_find = load("sss_bend_extended_find_steps_5c2c5042f");
    const auto load_optional = [&](const char* name) {
      ComPtr<ID3D11ComputeShader> shader;
      const auto code = ReadFile(before + "\\" + name + ".cso");
      if (!code.empty()) REQUIRE(SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, shader.GetAddressOf())));
      return shader;
    };
    const ComPtr<ID3D11ComputeShader> slots_2304 = load_optional("sss_bend_steps_2304");
    const ComPtr<ID3D11ComputeShader> slots_2304_find = load_optional("sss_bend_extended_find_steps_2304");
    auto& objects = sss::internal::runtime.objects;
    // The add-on's shares at 4K: 2/3 and 1/3 exact (2560, 1280), and the rows G1 had (66.7% and 33.3%: 2561, 1279).
    struct TimeShare {
      const char* name;
      UINT width;
      UINT height;
    };
    for (const TimeShare& share : {TimeShare{"100%", 3840, 2160}, TimeShare{"66.7%", 2561, 1441}, TimeShare{"2/3", 2560, 1440}, TimeShare{"58%", 2227, 1253},
                                   TimeShare{"50%", 1920, 1080}, TimeShare{"33.3%", 1279, 719}, TimeShare{"1/3", 1280, 720}}) {
      const UINT w = share.width;
      const UINT h = share.height;
      const Frame render = MakeFrame(w, h, boxes, {0.25f, -0.55f, -0.8f});
      Frame display = MakeFrame(3840, 2160, {}, render.sun);
      std::vector<float> depth(3840 * 2160, 1.f);
      for (UINT y = 0; y < h; ++y) std::copy_n(render.picture.depth.begin() + (y * w), w, depth.begin() + (y * 3840));
      display.depth = MakeTexture(3840, 2160, DXGI_FORMAT_R32_FLOAT, depth.data(), 3840 * 4);
      display.camera_constants = render.camera_constants;
      display.lighting = render.lighting;
      twattila::frame_history::render_width = (w != 3840) ? w : 0;
      twattila::frame_history::render_height = (w != 3840) ? h : 0;
      for (const auto* row : {&base, &softer}) {
        const double ours = GpuMilliseconds([&] { RunFrame(display, *row); });
        const std::array<ComPtr<ID3D11ComputeShader>, 4> module_shaders = {objects.bend, objects.bend_steps, objects.bend_extended[2],
                                                                           objects.bend_extended_steps[2]};
        // A form's shaders in the module's place (the 100% row's plain shaders stay: there the steps ones never run).
        const auto timed = [&](const ComPtr<ID3D11ComputeShader>& plain, const ComPtr<ID3D11ComputeShader>& find) {
          objects.bend_steps = plain;
          objects.bend_extended_steps[2] = find;
          const double milliseconds = GpuMilliseconds([&] { RunFrame(display, *row); });
          objects.bend_steps = module_shaders[1];
          objects.bend_extended_steps[2] = module_shaders[3];
          return milliseconds;
        };
        const double aprime_form = timed(aprime, aprime_find);
        const double slots_form = (slots_2304 != nullptr && slots_2304_find != nullptr) ? timed(slots_2304, slots_2304_find) : -1.0;
        objects.bend = objects.bend_steps = today;
        objects.bend_extended[2] = objects.bend_extended_steps[2] = today_find;
        sss::internal::scale_control = {.samples_by_share = true, .thickness_by_share = true};
        const double before_form = GpuMilliseconds([&] { RunFrame(display, *row); });
        sss::internal::scale_control = {};
        objects.bend = module_shaders[0];
        objects.bend_steps = module_shaders[1];
        objects.bend_extended[2] = module_shaders[2];
        objects.bend_extended_steps[2] = module_shaders[3];
        std::printf("   render scale %-5s (%4u px) %-11s O2 %.3f ms, today's A' %.3f ms, C %.3f ms (O2 / A' %.3f, O2 - C %+.3f)", share.name, w,
                    (row == &base) ? "plain" : "Softer 0.5", ours, aprime_form, before_form, ours / aprime_form, ours - before_form);
        if (slots_form >= 0.0) std::printf(", O2 2304 slots %.3f ms", slots_form);
        std::printf("\n");
      }
    }
    twattila::frame_history::render_width = 0;
    twattila::frame_history::render_height = 0;
  }
  // Torches: the frame's first light draw (the pick and the march over last frame's 16 lights), near (9 to 11 m: each
  // one's reach covers most of the screen) and at a battle's distance (40 to 60 m).
  for (const float distance : {9.f, 40.f}) {
    std::vector<LightInstance> torches;
    for (int index = 0; index < 16; ++index) {
      const float x = (-6.f + (0.8f * static_cast<float>(index))) * distance / 9.f;
      torches.push_back({.colour = {4.f, 2.f, 0.5f, 0.f}, .light = {x, -1.2f, distance + (static_cast<float>(index % 3) * distance / 9.f), 6.f}});
    }
    const auto buffer = MakeBuffer(static_cast<UINT>(torches.size() * sizeof(LightInstance)), D3D11_BIND_VERTEX_BUFFER, torches.data());
    const lights::Instances drawn = {.buffer = buffer.Get(), .first = 0, .count = 16, .stride = sizeof(LightInstance),
                                     .position = offsetof(LightInstance, light), .colour = offsetof(LightInstance, colour)};
    for (const uint32_t wanted : {4u, 8u, 16u}) {
      sss::Settings settings = base;
      settings.lights = wanted;
      RunFrame(frame, settings);
      lights::OnLightDraw(context.Get(), drawn);
      const double march = GpuMilliseconds([&] {
        ++sss::internal::runtime.frame;
        sss::internal::runtime.computed_frame = sss::internal::runtime.frame;
        lights::OnLightDraw(context.Get(), drawn);
      });
      std::printf("   Torch and Fire Shadows %2u, lights %2.0f m away: %.3f ms at the first light draw\n", wanted, distance, march);
    }
    if (distance == 40.f) {
      // CPU: a later light draw (its copy and the binds).
      const auto start = std::chrono::steady_clock::now();
      for (int repeat = 0; repeat < 1000; ++repeat) lights::OnLightDraw(context.Get(), drawn);
      const double light_draw = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / 1000.0;
      std::printf("   a later light draw: %.2f us CPU\n", light_draw);
    }
  }
  // A battle's light count (BattleLights: 4096 candidates in 128 draws): the first light draw with today's sss_lights and
  // with f6408aa1's.
  const auto battle = BattleLights();
  const auto battle_buffer = MakeBuffer(static_cast<UINT>(battle.size() * sizeof(LightInstance)), D3D11_BIND_VERTEX_BUFFER, battle.data());
  const auto old_bytes = ReadFile(before + "\\sss_lights_f6408aa1.cso");
  REQUIRE(!old_bytes.empty());
  for (const bool old : {false, true}) {
    if (old) REQUIRE(SUCCEEDED(device->CreateComputeShader(old_bytes.data(), old_bytes.size(), nullptr, sss::internal::runtime.objects.lights.shader.ReleaseAndGetAddressOf())));
    for (const uint32_t wanted : {8u, 32u}) {
      REQUIRE(LightFrames(frame, std::span<const LightInstance>(battle), wanted, 0.f, 40));
      const double first_draw = GpuMilliseconds([&] {
        ++sss::internal::runtime.frame;
        sss::internal::runtime.computed_frame = sss::internal::runtime.frame;
        for (size_t first = 0; first < battle.size(); first += 40) {
          lights::OnLightDraw(context.Get(), {.buffer = battle_buffer.Get(), .first = static_cast<UINT>(first * sizeof(LightInstance)), .count = 40,
                                              .stride = sizeof(LightInstance), .position = offsetof(LightInstance, light),
                                              .colour = offsetof(LightInstance, colour)});
        }
      });
      std::printf("   Torch and Fire Shadows %2u, %zu lights in %zu draws (%s sss_lights): %.3f ms a frame of light draws\n", wanted, battle.size(),
                  battle.size() / 40, old ? "f6408aa1's" : "today's", first_draw);
      if (old) continue;
      // Its parts: the draws' copies alone (no depth this frame: nothing picked or marched), the pick alone and the march
      // alone (each with the bindings OnLightDraw gives it, over the frame's records just made).
      const double copies = GpuMilliseconds([&] {
        ++sss::internal::runtime.frame;
        for (size_t first = 0; first < battle.size(); first += 40) {
          lights::OnLightDraw(context.Get(), {.buffer = battle_buffer.Get(), .first = static_cast<UINT>(first * sizeof(LightInstance)), .count = 40,
                                              .stride = sizeof(LightInstance), .position = offsetof(LightInstance, light),
                                              .colour = offsetof(LightInstance, colour)});
        }
      });
      REQUIRE(LightFrames(frame, std::span<const LightInstance>(battle), wanted, 0.f, 40));
      auto& objects = sss::internal::runtime.objects;
      auto& torch = objects.lights;
      const size_t last = 1 - torch.current;
      const double pick = GpuMilliseconds([&] {
        const sss::internal::ComputeState kept(context.Get());
        sss::internal::ComputeState::Bind(context.Get(), torch.shader.Get(), {torch.instances_srv[last].Get(), torch.draws_srv.Get()},
                                          {torch.dispatches_uav.Get(), torch.coordinates_uav.Get(), torch.arguments_uav.Get(), torch.list_uav.Get()},
                                          {torch.camera.Get(), nullptr, objects.settings.Get(), nullptr, torch.settings.Get()});
        context->Dispatch(1, 1, 1);
      });
      const double march = GpuMilliseconds([&] {
        const sss::internal::ComputeState kept(context.Get());
        sss::internal::ComputeState::Bind(context.Get(), objects.bend_extended[sss::internal::ExtendedVariant(false, false)].Get(),
                                          {objects.depth_srv.Get(), objects.parameters_srv.Get(), nullptr, nullptr, torch.dispatches_srv.Get(),
                                           torch.coordinates_srv.Get()},
                                          {nullptr, nullptr, nullptr, torch.mask_uav.Get()},
                                          {torch.camera.Get(), nullptr, objects.settings.Get(), torch.mode.Get(), torch.settings.Get()});
        context->CSSetSamplers(0, 1, objects.border.GetAddressOf());
        context->DispatchIndirect(torch.arguments.Get(), 0);
      });
      std::printf("      its parts: %zu copies %.3f ms, the pick %.3f ms, the march %.3f ms\n", battle.size() / 40, copies, pick, march);
    }
  }
}

// ---- 11. Screen steps (O-SS5) ---------------------------------------------------------------------------------------
// plan_1117 designs\sss_scale_thickness.md section 5. Each scene is traced at 960 x 540 (the reference: the 100% march)
// and at 67%, 50% and 33% into the top-left of the display's depth; a render pixel is compared with the reference at
// its centre's screen position. The A/A noise: the 100% march over a depth traced half a render pixel off (DLSS's
// jitter), against the reference. W2, W4 and W6-W8 look straight ahead at walls facing the camera: their depth has no
// step along the rays' minor axis, so Bend's minor-axis bias (the design's F7: a render texel's depth step, 1 / share of
// 100%'s; no fix, W10 numbers it) stays out of what they measure.

// Case 11's other shaders (run.ps1's csos): today's sss_bend and sss_bend_extended_find (bc423ef9d, before O-SS5: a
// sample a render pixel) and the module's with BEND_SSS_STEP_CONTROL 1 (nearest sub-samples), 2 (the near-light window
// in render pixels), 3 (the found caster in render steps), 4 (half a render pixel a sample at any scale), 5 (the grid
// pass's rounds from the first). apd_ O2 (round_notes designs\sss_aprime_cheaper.md: the module's steps shaders, one
// shared screen grid): A' as 5c2c5042f had it (every lane its own samples, `aprime`), the numbers its section 4 bounds.
struct StepShaders {
  ComPtr<ID3D11ComputeShader> today;
  ComPtr<ID3D11ComputeShader> today_find;
  ComPtr<ID3D11ComputeShader> nearest;
  ComPtr<ID3D11ComputeShader> render_window;
  ComPtr<ID3D11ComputeShader> render_find;
  ComPtr<ID3D11ComputeShader> half_step;
  ComPtr<ID3D11ComputeShader> ascending;
  ComPtr<ID3D11ComputeShader> aprime;
  ComPtr<ID3D11ComputeShader> aprime_find;
};

// A march's form: its plain and find shaders (null: the module's) and the host's scale control (the add-on's by default).
struct StepForm {
  ID3D11ComputeShader* plain = nullptr;
  ID3D11ComputeShader* find = nullptr;
  sss::internal::ScaleControl control = {};
};

constexpr UINT DISPLAY_W = 960;
constexpr UINT DISPLAY_H = 540;

struct StepResult {
  std::vector<uint8_t> occlusion;  // the render part's, row by row
  std::vector<uint16_t> penumbra;  // with Softer Away from the Caster
  std::array<float, 4> light = {};  // sss_setup's light coordinate (render pixels)
};

// A 960 x 540 picture's value at each render pixel's centre (w x h render).
static std::vector<uint8_t> AtRenderPixels(const std::vector<uint8_t>& full, UINT w, UINT h) {
  std::vector<uint8_t> values(w * h);
  for (UINT y = 0; y < h; ++y) {
    for (UINT x = 0; x < w; ++x) {
      const auto fx = static_cast<UINT>((static_cast<float>(x) + 0.5f) * static_cast<float>(DISPLAY_W) / static_cast<float>(w));
      const auto fy = static_cast<UINT>((static_cast<float>(y) + 0.5f) * static_cast<float>(DISPLAY_H) / static_cast<float>(h));
      values[(y * w) + x] = full[(fy * DISPLAY_W) + fx];
    }
  }
  return values;
}

// One frame of `render` at its share of 960 x 540 (its depth in the top-left of the display's); `full_steps`: its depth
// nearest-upsampled to 960 x 540 and marched at 100% instead (its result still the render pixels', at their centres).
static StepResult MarchAt(const Frame& render, const sss::Settings& settings, const StepForm& form = {}, bool full_steps = false) {
  auto& objects = sss::internal::runtime.objects;
  const UINT w = render.camera.width;
  const UINT h = render.camera.height;
  const Frame* frame = &render;
  Frame display;
  if (w != DISPLAY_W) {
    display = MakeFrame(DISPLAY_W, DISPLAY_H, {}, render.sun);
    std::vector<float> depth(DISPLAY_W * DISPLAY_H, 1.f);
    if (full_steps) {
      for (UINT y = 0; y < DISPLAY_H; ++y) {
        for (UINT x = 0; x < DISPLAY_W; ++x) depth[(y * DISPLAY_W) + x] = render.picture.depth[((y * h / DISPLAY_H) * w) + (x * w / DISPLAY_W)];
      }
    } else {
      for (UINT y = 0; y < h; ++y) std::copy_n(render.picture.depth.begin() + (y * w), w, depth.begin() + (y * DISPLAY_W));
    }
    display.depth = MakeTexture(DISPLAY_W, DISPLAY_H, DXGI_FORMAT_R32_FLOAT, depth.data(), DISPLAY_W * 4);
    display.camera_constants = render.camera_constants;
    display.lighting = render.lighting;
    frame = &display;
  }
  const bool scaled = (w != DISPLAY_W) && !full_steps;
  twattila::frame_history::render_width = scaled ? w : 0;
  twattila::frame_history::render_height = scaled ? h : 0;
  sss::internal::scale_control = form.control;
  REQUIRE(sss::internal::Prepare(device.Get(), DISPLAY_W, DISPLAY_H));
  const std::array<ComPtr<ID3D11ComputeShader>, 4> module_shaders = {objects.bend, objects.bend_steps, objects.bend_extended[2],
                                                                     objects.bend_extended_steps[2]};
  if (form.plain != nullptr) objects.bend = objects.bend_steps = form.plain;
  if (form.find != nullptr) objects.bend_extended[2] = objects.bend_extended_steps[2] = form.find;
  RunFrame(*frame, settings);
  objects.bend = module_shaders[0];
  objects.bend_steps = module_shaders[1];
  objects.bend_extended[2] = module_shaders[2];
  objects.bend_extended_steps[2] = module_shaders[3];
  sss::internal::scale_control = {};
  twattila::frame_history::render_width = 0;
  twattila::frame_history::render_height = 0;
  StepResult result;
  const auto occlusion = ReadView<uint8_t>(objects.occlusion_srv.Get());
  std::vector<uint16_t> penumbra;
  if (settings.sun_disk > 0.f) penumbra = ReadView<uint16_t>(objects.penumbra_srv.Get());
  if (full_steps) {
    result.occlusion = AtRenderPixels(occlusion, w, h);
    return result;
  }
  for (UINT y = 0; y < h; ++y) {
    result.occlusion.insert(result.occlusion.end(), occlusion.begin() + (y * DISPLAY_W), occlusion.begin() + (y * DISPLAY_W) + w);
    if (!penumbra.empty()) result.penumbra.insert(result.penumbra.end(), penumbra.begin() + (y * DISPLAY_W), penumbra.begin() + (y * DISPLAY_W) + w);
  }
  const auto parameters = ReadBuffer<float>(objects.parameters.Get());
  std::copy_n(parameters.begin(), 4, result.light.begin());
  return result;
}

// The ground y = GROUND_Y + height(z) alone (W5's slopes), ray-marched into `frame`'s picture and depth (the sky 1).
template <typename Height>
static void TraceHeight(Frame* frame, Height height) {
  const UINT w = frame->camera.width;
  const UINT h = frame->camera.height;
  const float tan_half_x = TAN_HALF_Y * static_cast<float>(w) / static_cast<float>(h);
  auto& picture = frame->picture;
  for (UINT y = 0; y < h; ++y) {
    for (UINT x = 0; x < w; ++x) {
      const float ndc_x = ((static_cast<float>(x) + 0.5f + frame->camera.jitter) / static_cast<float>(w) * 2.f) - 1.f;
      const float ndc_y = 1.f - ((static_cast<float>(y) + 0.5f + frame->camera.jitter) / static_cast<float>(h) * 2.f);
      const Vector view_direction = {ndc_x * tan_half_x, ndc_y * TAN_HALF_Y, 1.f};
      Vector direction = {};
      for (int j = 0; j < 3; ++j) {
        direction[j] = (view_direction[0] * frame->camera.view[j][0]) + (view_direction[1] * frame->camera.view[j][1])
                       + (view_direction[2] * frame->camera.view[j][2]);
      }
      const auto above = [&](float t) { return (direction[1] * t) - (GROUND_Y + height(direction[2] * t)); };
      float t0 = 0.f;
      float t1 = 0.f;
      bool hit = false;
      for (float t = 0.1f; t < 300.f; t += 0.005f + (0.004f * t)) {
        if (above(t) < 0.f) {
          t1 = t;
          hit = true;
          break;
        }
        t0 = t;
      }
      const size_t pixel = (y * w) + x;
      picture.depth[pixel] = 1.f;
      picture.hit[pixel] = -2;
      if (!hit) continue;
      for (int step = 0; step < 30; ++step) {
        const float middle = 0.5f * (t0 + t1);
        if (above(middle) < 0.f) {
          t1 = middle;
        } else {
          t0 = middle;
        }
      }
      const Vector p = {direction[0] * t1, direction[1] * t1, direction[2] * t1};
      const auto clip = Project(frame->camera, p);
      picture.depth[pixel] = clip[2] / clip[3];
      picture.hit[pixel] = -1;
      picture.position[pixel] = p;
    }
  }
  frame->depth = MakeTexture(w, h, DXGI_FORMAT_R32_FLOAT, picture.depth.data(), w * 4);
}

// Case 12 (X30, fidelity screen_space.md SS-B5): Softness's sss_blur near the far plane. A projection whose far plane is
// 16 m (A = F / (F - N) = 1.0063), the camera level: flat ground 5 to 9 m out (40% to 55% of the far plane, where Bend's
// shadows fade out and 1 - depth is about half of A - depth), posts' shadows on it. Every ground pixel whose 9 taps
// (Softness 2) all lie on the ground within 2% of its distance (98% of it: float margin) keeps them all: its blurred
// occlusion the tent's mean of the 9 (1 / 255 apart at most). CONTROL (must fail): bc423ef9d's sss_blur (1 / (1 - depth),
// about a 1% test there) rejects taps on that ground.
static void TestSameSurface(const std::string& before_folder) {
  std::printf("12. same surface near the far plane (X30)\n");
  MakeDevice(D3D_DRIVER_TYPE_WARP);
  const auto code = ReadFile(before_folder + "\\sss_blur_bc423ef9d.cso");
  REQUIRE(!code.empty());
  ComPtr<ID3D11ComputeShader> today;
  REQUIRE(SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, today.GetAddressOf())));
  constexpr UINT W = 960;
  constexpr UINT H = 540;
  constexpr int SPACING = 2;
  const std::array<Box, 4> posts = {{{.low = {-2.f, -2.f, 6.f}, .high = {-1.8f, 0.f, 6.2f}},
                                     {.low = {0.f, -2.f, 7.f}, .high = {0.2f, 0.f, 7.2f}},
                                     {.low = {1.5f, -2.f, 5.5f}, .high = {1.7f, 0.f, 5.7f}},
                                     {.low = {-0.8f, -2.f, 8.f}, .high = {-0.6f, 0.f, 8.2f}}}};
  const Frame frame = MakeFrame(W, H, posts, {1.f, -0.5f, 0.3f}, 0.f, 0.f, 16.f);
  sss::Settings settings = Defaults();
  settings.softness = static_cast<float>(SPACING);
  auto& objects = sss::internal::runtime.objects;
  // Pixels apart from the mean of all 9 taps, of those whose taps all lie on the ground within 2% of its distance; how
  // many of those have taps of other occlusion (`varied`).
  const auto apart = [&](int* checked, int* varied) {
    REQUIRE(sss::internal::Prepare(device.Get(), W, H));
    RunFrame(frame, settings);
    const auto occlusion = ReadView<uint8_t>(objects.occlusion_srv.Get());
    const auto soft = ReadView<uint8_t>(objects.soft_srv.Get());
    int count = 0;
    *checked = 0;
    *varied = 0;
    for (int y = 0; y < static_cast<int>(H); ++y) {
      for (int x = 0; x < static_cast<int>(W); ++x) {
        const size_t pixel = (static_cast<size_t>(y) * W) + x;
        if (frame.picture.hit[pixel] != -1) continue;
        const float distance = frame.picture.position[pixel][2];
        double sum = 0.0;
        double total = 0.0;
        bool kept = true;
        for (int dy = -1; dy <= 1 && kept; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            const int tx = std::clamp(x + (dx * SPACING), 0, static_cast<int>(W) - 1);
            const int ty = std::clamp(y + (dy * SPACING), 0, static_cast<int>(H) - 1);
            const size_t tap = (static_cast<size_t>(ty) * W) + tx;
            if (frame.picture.hit[tap] != -1 || std::abs(frame.picture.position[tap][2] - distance) > 0.98f * 0.02f * distance) {
              kept = false;
              break;
            }
            const double tent = 1.0 / static_cast<double>(1 << (std::abs(dx) + std::abs(dy)));
            sum += tent * occlusion[tap];
            total += tent;
          }
        }
        if (!kept) continue;
        ++*checked;
        *varied += (occlusion[pixel] * total != sum) ? 1 : 0;
        count += (std::abs(static_cast<double>(soft[pixel]) - (sum / total)) > 1.0) ? 1 : 0;
      }
    }
    return count;
  };
  int checked = 0;
  int varied = 0;
  const int ours = apart(&checked, &varied);
  const ComPtr<ID3D11ComputeShader> module_blur = objects.blur;
  objects.blur = today;
  int control_checked = 0;
  int control_varied = 0;
  const int control = apart(&control_checked, &control_varied);
  objects.blur = module_blur;
  std::printf("   ground pixels with every tap on the surface %d (%d with taps of other occlusion): apart from all 9 taps' mean ours %d, CONTROL bc423ef9d's %d\n",
              checked, varied, ours, control);
  CHECK(checked > 10000 && varied > 100 && control_checked == checked);
  CHECK(ours == 0);
  CHECK(control > varied / 10);
}

static void TestScreenSteps(const std::string& before_folder) {
  std::printf("11. screen steps (O-SS5)\n");
  MakeDevice(D3D_DRIVER_TYPE_WARP);
  const auto load = [&](const char* name) {
    const auto code = ReadFile(before_folder + "\\" + name + ".cso");
    REQUIRE(!code.empty());
    ComPtr<ID3D11ComputeShader> shader;
    REQUIRE(SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, shader.GetAddressOf())));
    return shader;
  };
  const StepShaders shaders = {.today = load("sss_bend_bc423ef9d"),
                               .today_find = load("sss_bend_extended_find_bc423ef9d"),
                               .nearest = load("sss_bend_control1"),
                               .render_window = load("sss_bend_control2"),
                               .render_find = load("sss_bend_extended_find_control3"),
                               .half_step = load("sss_bend_control4"),
                               .ascending = load("sss_bend_control5"),
                               .aprime = load("sss_bend_steps_5c2c5042f"),
                               .aprime_find = load("sss_bend_extended_find_steps_5c2c5042f")};
  // Today's A' (5c2c5042f): its steps shaders in the module's place (the host's scale control, the add-on's).
  const StepForm aprime_form = {.plain = shaders.aprime.Get(), .find = shaders.aprime_find.Get()};
  // C, the form before O-SS5: today's sss_bend, samples x share, thickness and threshold / share.
  const StepForm before_form = {.plain = shaders.today.Get(), .find = shaders.today_find.Get(),
                                .control = {.samples_by_share = true, .thickness_by_share = true}};
  // Render steps with the row's thickness (W3's control): today's sss_bend, samples x share, threshold / share.
  const StepForm render_steps = {.plain = shaders.today.Get(), .find = shaders.today_find.Get(), .control = {.samples_by_share = true}};
  const auto ground_pixels = [](const Frame& frame) { return std::ranges::count(frame.picture.hit, -1); };
  // The render sizes at 67%, 50% and 33% of 960 x 540.
  struct Share {
    UINT width;
    UINT height;
    const char* name;
  };
  constexpr std::array<Share, 3> SHARES = {{{640, 360, "67%"}, {480, 270, "50%"}, {320, 180, "33%"}}};
  // W11 (apd_ O2, designs\sss_aprime_cheaper.md 4): shares with no exact grid (one grid point a screen pixel).
  constexpr std::array<Share, 2> W11_SHARES = {{{557, 313, "58%"}, {641, 361, "66.8%"}}};
  // The shader's exact grid (the share p / q with q in 2..4): today's A' sample positions.
  const auto exact_grid = [](UINT width) { return ((width * 2) % DISPLAY_W == 0) || ((width * 3) % DISPLAY_W == 0) || ((width * 4) % DISPLAY_W == 0); };
  // O2 against today's A' (5c2c5042f), runs of each (case 1's rule: a pixel apart only by its nearest pair of runs):
  // the pixels apart by more than `threshold` (of 255).
  const auto apart_by = [](const std::vector<std::vector<uint8_t>>& ours, const std::vector<std::vector<uint8_t>>& theirs, int threshold) {
    int count = 0;
    for (size_t pixel = 0; pixel < ours[0].size(); ++pixel) {
      int nearest = 255;
      for (const auto& run : ours) {
        for (const auto& other : theirs) nearest = std::min(nearest, std::abs(static_cast<int>(run[pixel]) - static_cast<int>(other[pixel])));
      }
      count += (nearest > threshold) ? 1 : 0;
    }
    return count;
  };
  // W11's number for ATN: of the pixels either form shadows by over 0.25, the share apart by over 0.25.
  const auto shadowed_apart = [](const std::vector<uint8_t>& ours, const std::vector<uint8_t>& theirs) {
    int shadowed = 0;
    int apart = 0;
    for (size_t pixel = 0; pixel < ours.size(); ++pixel) {
      if (std::max(ours[pixel], theirs[pixel]) < 64) continue;
      ++shadowed;
      apart += (std::abs(static_cast<int>(ours[pixel]) - static_cast<int>(theirs[pixel])) >= 64) ? 1 : 0;
    }
    return static_cast<double>(apart) / std::max(shadowed, 1);
  };
  // The reference and its A/A noise at a render size: the 100% march resampled at the render pixels, and the 100% march
  // over a depth traced half a render pixel off, resampled. `geometry` (ours, a deviation from the design's reference
  // where the render's geometry dominates: W2, W4): the 100% march over the render's depth nearest-upsampled, so
  // what a render pixel's point sample of thin geometry gains or loses (the design's F8, inherent) is in it too, and
  // the comparison holds the march alone.
  struct Reference {
    std::vector<uint8_t> occlusion;
    std::vector<uint8_t> jittered;
    std::vector<uint8_t> geometry;
  };
  const auto reference = [&](const std::function<Frame(UINT, UINT, float)>& scene, const sss::Settings& settings, UINT w, UINT h) {
    const auto full = MarchAt(scene(DISPLAY_W, DISPLAY_H, 0.f), settings).occlusion;
    const auto jittered = MarchAt(scene(DISPLAY_W, DISPLAY_H, 0.5f * static_cast<float>(DISPLAY_W) / static_cast<float>(w)), settings).occlusion;
    return Reference{.occlusion = AtRenderPixels(full, w, h),
                     .jittered = AtRenderPixels(jittered, w, h),
                     .geometry = MarchAt(scene(w, h, 0.f), settings, {}, true).occlusion};
  };

  // W1 identity: at 100% the plain and the extended march (with what Softer finds) as today's to the bit (case 1's 8 runs),
  // the hosts' 100% shaders (Bend's loops) and the steps' (sss_bend*_steps, ScreenStep 1) both. CONTROL (must differ):
  // half a render pixel a sample at 100%.
  {
    const std::array<Box, 3> casters = {{BOX_SCENE[0], {.low = {-6.f, -2.f, 20.f}, .high = {6.f, 0.f, 22.f}}, {.low = {3.f, -2.f, 10.f}, .high = {3.2f, 2.f, 10.2f}}}};
    const Frame frame = MakeFrame(DISPLAY_W, DISPLAY_H, casters, BOX_SUN);
    sss::Settings softer = Defaults();
    softer.sun_disk = 2.f;
    std::vector<std::vector<uint8_t>> ours;
    std::vector<std::vector<uint8_t>> today;
    std::vector<std::vector<uint8_t>> ours_extended;
    std::vector<std::vector<uint8_t>> today_extended;
    std::vector<std::vector<uint8_t>> steps;
    std::vector<std::vector<uint8_t>> steps_extended;
    std::vector<std::vector<uint8_t>> half_step;
    int penumbra_apart = 0;
    for (int run = 0; run < RUNS; ++run) {
      ours.push_back(MarchAt(frame, Defaults()).occlusion);
      steps.push_back(MarchAt(frame, Defaults(), {.plain = sss::internal::runtime.objects.bend_steps.Get()}).occlusion);
      steps_extended.push_back(MarchAt(frame, softer, {.find = sss::internal::runtime.objects.bend_extended_steps[2].Get()}).occlusion);
      today.push_back(MarchAt(frame, Defaults(), {.plain = shaders.today.Get()}).occlusion);
      const auto extended = MarchAt(frame, softer);
      const auto today_softer = MarchAt(frame, softer, {.find = shaders.today_find.Get()});
      ours_extended.push_back(extended.occlusion);
      today_extended.push_back(today_softer.occlusion);
      // The penumbra: today's (Bend's double writes aside, case 1's 0.2%).
      for (size_t pixel = 0; pixel < extended.penumbra.size(); ++pixel) {
        penumbra_apart += (extended.penumbra[pixel] != today_softer.penumbra[pixel]) ? 1 : 0;
      }
      half_step.push_back(MarchAt(frame, Defaults(), {.plain = shaders.half_step.Get()}).occlusion);
    }
    std::printf("   W1 at 100%%:\n");
    CompareRuns("   plain against today's (bc423ef9d)", ours, today);
    CompareRuns("   extended (Softer's find) against today's", ours_extended, today_extended);
    CompareRuns("   steps' plain (sss_bend_steps) against today's", steps, today);
    CompareRuns("   steps' extended (sss_bend_extended_find_steps) against today's", steps_extended, today_extended);
    std::printf("   extended's penumbra: %d pixel-runs apart from today's\n", penumbra_apart);
    CHECK(penumbra_apart < static_cast<int>(RUNS * DISPLAY_W * DISPLAY_H / 500));
    CompareRuns("   CONTROL half a render pixel a sample against today's", half_step, today, false);
  }

  // W2 thickness: a post 0.03 m deep before a wall 0.155 m behind its front (2.6 of Bend's windows at 100%, 0.005 of
  // the depth left), the sun behind the camera's side: the rays from the wall come nearer as they go, and pass behind the
  // post within the window only at some distance from it. Halo pixels: wall pixels occluded where the render geometry's
  // 100% march is lit. A' within the A/A noise's (x 1.5 + 0.1% of the wall) at every share; CONTROL C (thickness /
  // share) over it at 50% and 33%. Against the full depth's reference (numbers): the render's point-sampled post.
  {
    const std::array<Box, 2> post_wall = {{{.low = {-3.f, -2.f, 12.f}, .high = {-2.85f, 1.f, 12.03f}},
                                           {.low = {-4.f, -2.f, 12.155f}, .high = {8.f, 2.f, 12.5f}}}};
    const Vector sun = {1.f, -0.24f, 0.15f};
    const auto scene = [&](UINT w, UINT h, float jitter) { return MakeFrame(w, h, post_wall, sun, jitter, 0.f); };
    for (const auto& share : {SHARES[0], SHARES[1], SHARES[2], W11_SHARES[0], W11_SHARES[1]}) {
      const bool exact = exact_grid(share.width);
      const Frame render = scene(share.width, share.height, 0.f);
      const auto ref = reference(scene, Defaults(), share.width, share.height);
      const auto wall = std::ranges::count(render.picture.hit, 1);
      const auto halo = [&](const std::vector<uint8_t>& occlusion, const std::vector<uint8_t>& lit) {
        int count = 0;
        for (size_t pixel = 0; pixel < occlusion.size(); ++pixel) {
          count += (render.picture.hit[pixel] == 1 && occlusion[pixel] > 127 && lit[pixel] <= 127) ? 1 : 0;
        }
        return count;
      };
      const auto ours_occlusion = MarchAt(render, Defaults()).occlusion;
      const auto aprime_occlusion = MarchAt(render, Defaults(), aprime_form).occlusion;
      const auto before_occlusion = MarchAt(render, Defaults(), before_form).occlusion;
      const int noise = halo(ref.jittered, ref.occlusion);
      const int ours = halo(ours_occlusion, ref.geometry);
      const int aprime = halo(aprime_occlusion, ref.geometry);
      const int before = halo(before_occlusion, ref.geometry);
      const double bound = (1.5 * noise) + (0.001 * static_cast<double>(wall));
      std::printf("   W2%s %s: halo pixels (of %lld wall pixels) O2 %d, today's A' %d, C %d, A/A %d (bound %.0f); against the full depth's O2 %d, C %d",
                  exact ? "" : " (W11)", share.name, static_cast<long long>(wall), ours, aprime, before, noise, bound, halo(ours_occlusion, ref.occlusion),
                  halo(before_occlusion, ref.occlusion));
      CHECK(ours <= bound);
      if (exact) {
        // O2 within 2 pixels of today's A' (designs\sss_aprime_cheaper.md 4).
        std::printf("\n");
        CHECK(ours <= aprime + 2);
        if (share.width < 640) CHECK(before > bound);
      } else {
        std::printf("; shadowed pixels apart from today's A' by over 0.25: %.4f\n", shadowed_apart(ours_occlusion, aprime_occlusion));
      }
    }
  }

  // W3 comb: posts one render pixel wide at 33% (0.064 m) every 0.5 m, the sun low from the side. A comb row: its
  // shadow coverage (occluded ground pixels of the fence's shadow) differs from both neighbours' by more than half. A'
  // within the reference's and A/A's most + 5% of the rows; CONTROL (render steps, the row's thickness) over it.
  // W10 (numbers for ATN): the shadows' mass on it at 33% with Bend's BilinearSamplingOffsetMode off and on.
  {
    std::vector<Box> fence;
    for (float x = -3.f; x <= 3.01f; x += 0.5f) fence.push_back({.low = {x, -2.f, 10.f}, .high = {x + 0.064f, -0.5f, 10.064f}});
    const Vector sun = {0.9f, -0.3f, 0.3f};
    const auto scene = [&](UINT w, UINT h, float jitter) { return MakeFrame(w, h, fence, sun, jitter); };
    const Share& share = SHARES[2];
    const Frame render = scene(share.width, share.height, 0.f);
    const auto ref = reference(scene, Defaults(), share.width, share.height);
    const auto in_shadow_area = [&](size_t pixel) {
      const Vector& p = render.picture.position[pixel];
      return render.picture.hit[pixel] == -1 && p[0] > -3.f && p[0] < 6.f && p[2] > 9.f && p[2] < 14.f;
    };
    const auto comb = [&](const std::vector<uint8_t>& occlusion) {
      std::vector<int> coverage(share.height, 0);
      for (size_t pixel = 0; pixel < occlusion.size(); ++pixel) {
        if (in_shadow_area(pixel) && occlusion[pixel] > 127) ++coverage[pixel / share.width];
      }
      const auto apart = [](int a, int b) { return std::abs(a - b) > (0.5 * std::max(a, b)); };
      int rows = 0;
      int combed = 0;
      for (UINT y = 1; y + 1 < share.height; ++y) {
        if (std::max({coverage[y - 1], coverage[y], coverage[y + 1]}) == 0) continue;
        ++rows;
        combed += (apart(coverage[y], coverage[y - 1]) && apart(coverage[y], coverage[y + 1])) ? 1 : 0;
      }
      return (rows > 0) ? (static_cast<double>(combed) / rows) : 0.0;
    };
    const double reference_comb = comb(ref.occlusion);
    const double noise = comb(ref.jittered);
    const double ours = comb(MarchAt(render, Defaults()).occlusion);
    const double aprime = comb(MarchAt(render, Defaults(), aprime_form).occlusion);
    const double control = comb(MarchAt(render, Defaults(), render_steps).occlusion);
    const double before = comb(MarchAt(render, Defaults(), before_form).occlusion);
    const double bound = std::max(reference_comb, noise) + 0.05;
    std::printf("   W3 33%%: comb rows O2 %.3f, today's A' %.3f, render steps %.3f, C %.3f, reference %.3f, A/A %.3f (bound %.3f)\n", ours, aprime,
                control, before, reference_comb, noise, bound);
    CHECK(ours <= bound);
    CHECK(ours <= aprime + 0.005);
    CHECK(control > bound);
    const auto mass = [&](const std::vector<uint8_t>& occlusion) {
      double sum = 0.0;
      for (size_t pixel = 0; pixel < occlusion.size(); ++pixel) sum += in_shadow_area(pixel) ? (occlusion[pixel] / 255.0) : 0.0;
      return sum;
    };
    sss::Settings along_ray = Defaults();
    along_ray.sample_along_ray = true;
    std::printf("   W10 33%%: the fence's shadow mass (render pixels) BilinearSamplingOffsetMode off O2 %.1f (today's A' %.1f), on O2 %.1f (today's A' %.1f); the reference %.1f\n",
                mass(MarchAt(render, Defaults()).occlusion), mass(MarchAt(render, Defaults(), aprime_form).occlusion), mass(MarchAt(render, along_ray).occlusion),
                mass(MarchAt(render, along_ray, aprime_form).occlusion), mass(ref.occlusion));
  }

  // W4 lanes: one post k = 1..4 render pixels wide at 33% (0.03 m deep, touching a wall behind it), the sun nearly
  // parallel to the wall (towards the camera by 0.03: its rays from the wall run into the post). Its shadow's middle (the
  // wall 0.35 to 1.2 m from the post, past both forms' hard samples): the mean occlusion the render geometry's 100%
  // march's (a 3k-pixel post) within 0.05; CONTROL C at k = 1 apart (k / 4 of a shadow: Bend's 4 lanes).
  // W11: the same at 58% and 66.8% (no exact grid), O2 within 0.02 of today's A'.
  for (int row = 0; row < 12; ++row) {
    const Share& share = (row < 4) ? SHARES[2] : W11_SHARES[(row - 4) / 4];
    const int k = (row % 4) + 1;
    const bool exact = exact_grid(share.width);
    // A render pixel's width at the post (view depth ~11 m).
    const float view_z = 12.f;
    const float width = 2.f * view_z * TAN_HALF_Y * static_cast<float>(share.width) / static_cast<float>(share.height) / static_cast<float>(share.width)
                        * static_cast<float>(k);
    const std::array<Box, 2> post_wall = {{{.low = {-3.f, -2.f, 12.f}, .high = {-3.f + width, 1.f, 12.03f}},
                                           {.low = {-4.f, -2.f, 12.03f}, .high = {8.f, 2.f, 12.5f}}}};
    const Vector sun = {1.f, -0.24f, 0.03f};
    const auto scene = [&](UINT w, UINT h, float jitter) { return MakeFrame(w, h, post_wall, sun, jitter, 0.f); };
    const Frame render = scene(share.width, share.height, 0.f);
    const auto ref = reference(scene, Defaults(), share.width, share.height);
    const Vector to_sun = {-render.sun[0], -render.sun[1], -render.sun[2]};
    const auto middle = [&](const std::vector<uint8_t>& occlusion) {
      double sum = 0.0;
      int count = 0;
      for (size_t pixel = 0; pixel < occlusion.size(); ++pixel) {
        float distance = 0.f;
        if (render.picture.hit[pixel] != 1 || Occluder(std::span(post_wall).first(1), render.picture.position[pixel], to_sun, 1e30f, &distance) < 0) continue;
        if (distance < 0.35f || distance > 1.2f) continue;
        sum += occlusion[pixel] / 255.0;
        ++count;
      }
      return (count > 20) ? (sum / count) : -1.0;
    };
    const double reference_middle = middle(ref.geometry);
    const auto ours_occlusion = MarchAt(render, Defaults()).occlusion;
    const auto aprime_occlusion = MarchAt(render, Defaults(), aprime_form).occlusion;
    const double ours = middle(ours_occlusion);
    const double aprime = middle(aprime_occlusion);
    const double before = middle(MarchAt(render, Defaults(), before_form).occlusion);
    std::printf("   W4%s %s post %d render px (%.3f m): mid-shadow occlusion O2 %.3f, today's A' %.3f, C %.3f, the render geometry's 100%% %.3f; the full depth's %.3f, A/A %.3f",
                exact ? "" : " (W11)", share.name, k, width, ours, aprime, before, reference_middle, middle(ref.occlusion), middle(ref.jittered));
    if (exact) {
      // O2 within 0.005 of today's A' (designs\sss_aprime_cheaper.md 4).
      std::printf("\n");
      CHECK(reference_middle > 0.f && std::abs(ours - reference_middle) <= 0.05);
      CHECK(std::abs(ours - aprime) <= 0.005);
      if (k == 1) CHECK(std::abs(before - reference_middle) > 0.05);
    } else {
      std::printf("; shadowed pixels apart from today's A' by over 0.25: %.4f\n", shadowed_apart(ours_occlusion, aprime_occlusion));
      CHECK(aprime > 0.0 && std::abs(ours - aprime) <= 0.02);
    }
  }

  // W5 slopes at 33%: a lit 30 degree ramp (the sun 10 degrees over it) and a sine hill (the sun 15 degrees up). False
  // shadow: occluded where the CPU's ray to the sun is free for the pixel and its 8 neighbours (a render pixel off a
  // shadow's edge). A' within the reference's + the A/A noise + 0.1% of the ground; CONTROLS over it: debug14's
  // staircase (the nearer texel's depth at the sample's distance), on the hill the bilinear threshold not / share.
  {
    const float tan30 = 0.57735027f;
    const auto ramp = [tan30](float z) { return (z > 4.f) ? (tan30 * (z - 4.f)) : 0.f; };
    const auto hill = [](float z) { return 0.5f * std::sin(0.8f * z); };
    // The ramp's rays up its slope on screen (debug14's staircase along them), the hill's across it (their minor axis
    // along the slope: Bend's bilinear threshold at work).
    const Vector ramp_sun = {-0.2f, -0.6428f, -0.7661f};
    const Vector hill_sun = {0.6f, -0.26f, -0.76f};
    for (const bool is_ramp : {true, false}) {
      const Vector& sun = is_ramp ? ramp_sun : hill_sun;
      const auto scene = [&](UINT w, UINT h, float jitter) {
        Frame frame = MakeFrame(w, h, {}, sun, jitter);
        if (is_ramp) {
          TraceHeight(&frame, ramp);
        } else {
          TraceHeight(&frame, hill);
        }
        return frame;
      };
      const Share& share = SHARES[2];
      const Frame render = scene(share.width, share.height, 0.f);
      const auto ref = reference(scene, Defaults(), share.width, share.height);
      const Vector to_sun = Normalize({-sun[0], -sun[1], -sun[2]});
      std::vector<bool> lit(render.picture.hit.size(), false);
      for (size_t pixel = 0; pixel < lit.size(); ++pixel) {
        if (render.picture.hit[pixel] != -1) continue;
        const Vector& p = render.picture.position[pixel];
        bool free = true;
        for (float t = 0.02f; t < 60.f && free; t += 0.02f) {
          const float z = p[2] + (to_sun[2] * t);
          free = (p[1] + (to_sun[1] * t)) > (GROUND_Y + (is_ramp ? ramp(z) : hill(z)));
        }
        lit[pixel] = free;
      }
      std::vector<bool> lit_around(lit.size(), false);
      for (UINT y = 1; y + 1 < share.height; ++y) {
        for (UINT x = 1; x + 1 < share.width; ++x) {
          bool all = true;
          for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) all = all && lit[((y + dy) * share.width) + x + dx];
          }
          lit_around[(y * share.width) + x] = all;
        }
      }
      const auto false_share = [&](const std::vector<uint8_t>& occlusion) {
        int count = 0;
        int total = 0;
        for (size_t pixel = 0; pixel < occlusion.size(); ++pixel) {
          if (!lit_around[pixel]) continue;
          ++total;
          count += (occlusion[pixel] > 127) ? 1 : 0;
        }
        return static_cast<double>(count) / std::max(total, 1);
      };
      sss::internal::ScaleControl unscaled_threshold = {.edge_threshold_by_share = false};
      const double reference_share = false_share(ref.occlusion);
      const double noise = std::abs(false_share(ref.jittered) - reference_share);
      const double ours = false_share(MarchAt(render, Defaults()).occlusion);
      const double aprime = false_share(MarchAt(render, Defaults(), aprime_form).occlusion);
      const double nearest = false_share(MarchAt(render, Defaults(), {.plain = shaders.nearest.Get()}).occlusion);
      const double unscaled = false_share(MarchAt(render, Defaults(), {.control = unscaled_threshold}).occlusion);
      const double before = false_share(MarchAt(render, Defaults(), before_form).occlusion);
      const double bound = reference_share + noise + 0.001;
      std::printf("   W5 33%% %s: false shadow on lit ground O2 %.4f, today's A' %.4f, nearest %.4f, threshold not / share %.4f, C %.4f, reference %.4f, A/A %.4f (bound %.4f)\n",
                  is_ramp ? "ramp" : "hill", ours, aprime, nearest, unscaled, before, reference_share, noise, bound);
      CHECK(ours <= bound);
      // designs\sss_aprime_cheaper.md 4: the ramp within 0.0005 over today's A', the hill within 0.002 of it.
      CHECK(is_ramp ? (ours <= aprime + 0.0005) : (std::abs(ours - aprime) <= 0.002));
      CHECK(nearest > bound);
      if (!is_ramp) CHECK(unscaled > bound);
    }
  }

  // W6 near the light: the camera level, a wall facing it 12 m out, three posts 0.2 m before it (3.3 of Bend's windows
  // at 100%), the sun behind the camera (its point on the wall among the posts; a wall seen face on, none of grazing
  // ground's self-shadowing: Bend's acne there, run to run apart under WARP, was all W6's first scene measured). Wall
  // pixels within 200 screen pixels of the point whose shadow differs from the reference's (over or under half): A'
  // within the A/A noise's (x 1.5 + 0.1% of them) at every share. No CONTROL fails here (numbers for ATN, deviation from
  // the design's W6): within 200 / k pixels of the point, where Bend's widened window (1 / its distance in pixels, F6)
  // reaches a caster k windows out, the caster's shadow is under k x SurfaceThickness x 200 / k = 1 screen pixel long
  // (a ray's screen length scales with its distance to the point), so the render-pixel distance's window (1 / share
  // wider) adds at most a render pixel's fringe, within the A/A noise.
  {
    const std::array<Box, 4> wall_posts = {{{.low = {-6.f, -2.f, 12.f}, .high = {8.f, 4.f, 12.5f}},
                                            {.low = {1.6f, -0.8f, 11.7f}, .high = {2.f, -0.2f, 11.8f}},
                                            {.low = {3.2f, 0.3f, 11.7f}, .high = {3.6f, 0.9f, 11.8f}},
                                            {.low = {4.6f, -1.5f, 11.7f}, .high = {5.f, -0.9f, 11.8f}}}};
    const Vector sun = Normalize({0.25f, -0.05f, 1.f});
    const auto scene = [&](UINT w, UINT h, float jitter) { return MakeFrame(w, h, wall_posts, sun, jitter, 0.f); };
    for (const auto& share : SHARES) {
      const Frame render = scene(share.width, share.height, 0.f);
      const auto ref = reference(scene, Defaults(), share.width, share.height);
      const auto ours_result = MarchAt(render, Defaults());
      const float radius = 200.f * static_cast<float>(share.width) / static_cast<float>(DISPLAY_W);
      int wall = 0;
      const auto apart = [&](const std::vector<uint8_t>& occlusion) {
        int count = 0;
        wall = 0;
        for (UINT y = 0; y < share.height; ++y) {
          for (UINT x = 0; x < share.width; ++x) {
            const size_t pixel = (y * share.width) + x;
            const float dx = static_cast<float>(x) + 0.5f - ours_result.light[0];
            const float dy = static_cast<float>(y) + 0.5f - ours_result.light[1];
            if (render.picture.hit[pixel] != 0 || ((dx * dx) + (dy * dy)) > radius * radius) continue;
            ++wall;
            count += ((occlusion[pixel] > 127) != (ref.occlusion[pixel] > 127)) ? 1 : 0;
          }
        }
        return count;
      };
      const int noise = apart(ref.jittered);
      const int ours = apart(ours_result.occlusion);
      // O2 against today's A': the medians of 3 runs each (Bend's WARP acne varies run to run), within 3 pixels.
      const auto median3 = [&](const StepForm& form) {
        std::array<int, 3> counts = {};
        for (int& count : counts) count = apart(MarchAt(render, Defaults(), form).occlusion);
        std::ranges::sort(counts);
        return counts[1];
      };
      const int ours_median = median3({});
      const int aprime_median = median3(aprime_form);
      const int control = apart(MarchAt(render, Defaults(), {.plain = shaders.render_window.Get()}).occlusion);
      const int before = apart(MarchAt(render, Defaults(), before_form).occlusion);
      const double bound = (1.5 * noise) + (0.001 * static_cast<double>(wall));
      std::printf("   W6 %s: light at (%.0f, %.0f) render px; wall pixels within 200 screen px apart from the reference (of %d) O2 %d (median of 3 %d), today's A' median %d, render-pixel window %d, C %d, A/A %d (bound %.0f)\n",
                  share.name, ours_result.light[0], ours_result.light[1], wall, ours, ours_median, aprime_median, control, before, noise, bound);
      CHECK(ours_result.light[0] > 0.f && ours_result.light[0] < share.width && ours_result.light[1] > 0.f && ours_result.light[1] < share.height);
      CHECK(ours <= bound);
      CHECK(std::abs(ours_median - aprime_median) <= 3);
    }
  }

  // W7 fade and length: a tall post's shadow on a wall just behind it (0.5 windows), the sun parallel to the wall,
  // past the march's reach (240 screen pixels). On each render row of it, the screen x of its end (over half the row's
  // most occlusion) and of its fade's start (the last at 95% of it): their medians A' within 1 render pixel (+ the
  // A/A's) of the reference's at every share; CONTROL C's fade start (Bend's 8 fade samples in render pixels) apart.
  {
    const std::array<Box, 2> post_wall = {{{.low = {-3.f, -2.f, 12.f}, .high = {-2.85f, 6.f, 12.03f}},
                                           {.low = {-4.f, -2.f, 12.06f}, .high = {8.f, 2.f, 12.5f}}}};
    const Vector sun = {1.f, -0.24f, 0.03f};
    const auto scene = [&](UINT w, UINT h, float jitter) { return MakeFrame(w, h, post_wall, sun, jitter, 0.f); };
    // W11: the same at 58% and 66.8%, O2 within a render pixel of today's A'.
    for (const auto& share : {SHARES[0], SHARES[1], SHARES[2], W11_SHARES[0], W11_SHARES[1]}) {
      const bool exact = exact_grid(share.width);
      const Frame render = scene(share.width, share.height, 0.f);
      const auto ref = reference(scene, Defaults(), share.width, share.height);
      const float to_screen = static_cast<float>(DISPLAY_W) / static_cast<float>(share.width);
      // The medians over the rows of the end and the fade's start, screen pixels.
      const auto ends = [&](const std::vector<uint8_t>& occlusion) {
        std::vector<float> end_x;
        std::vector<float> fade_x;
        for (UINT y = 0; y < share.height; ++y) {
          int most = 0;
          for (UINT x = 0; x < share.width; ++x) {
            const size_t pixel = (y * share.width) + x;
            if (render.picture.hit[pixel] == 1) most = std::max<int>(most, occlusion[pixel]);
          }
          if (most < 64) continue;
          int end = -1;
          int fade = -1;
          for (UINT x = 0; x < share.width; ++x) {
            const size_t pixel = (y * share.width) + x;
            if (render.picture.hit[pixel] != 1) continue;
            if (occlusion[pixel] * 2 > most) end = static_cast<int>(x);
            if (occlusion[pixel] * 100 >= most * 95) fade = static_cast<int>(x);
          }
          end_x.push_back((static_cast<float>(end) + 0.5f) * to_screen);
          fade_x.push_back((static_cast<float>(fade) + 0.5f) * to_screen);
        }
        REQUIRE(end_x.size() > 5);
        std::ranges::nth_element(end_x, end_x.begin() + static_cast<ptrdiff_t>(end_x.size() / 2));
        std::ranges::nth_element(fade_x, fade_x.begin() + static_cast<ptrdiff_t>(fade_x.size() / 2));
        return std::array<float, 2>{end_x[end_x.size() / 2], fade_x[fade_x.size() / 2]};
      };
      const auto reference_ends = ends(ref.occlusion);
      const auto noise_ends = ends(ref.jittered);
      const auto ours_occlusion = MarchAt(render, Defaults()).occlusion;
      const auto aprime_occlusion = MarchAt(render, Defaults(), aprime_form).occlusion;
      const auto ours = ends(ours_occlusion);
      const auto aprime = ends(aprime_occlusion);
      const auto before = ends(MarchAt(render, Defaults(), before_form).occlusion);
      const float bound = to_screen + std::max(std::abs(noise_ends[0] - reference_ends[0]), std::abs(noise_ends[1] - reference_ends[1]));
      std::printf("   W7%s %s: end / fade start (screen px) O2 %.1f / %.1f, today's A' %.1f / %.1f, C %.1f / %.1f, reference %.1f / %.1f, A/A %.1f / %.1f (bound %.1f)",
                  exact ? "" : " (W11)", share.name, ours[0], ours[1], aprime[0], aprime[1], before[0], before[1], reference_ends[0], reference_ends[1],
                  noise_ends[0], noise_ends[1], bound);
      if (exact) {
        std::printf("\n");
        CHECK(std::abs(ours[0] - reference_ends[0]) <= bound && std::abs(ours[1] - reference_ends[1]) <= bound);
        if (share.width < 640) CHECK(std::abs(before[1] - reference_ends[1]) > bound);
      } else {
        std::printf("; shadowed pixels apart from today's A' by over 0.25: %.4f\n", shadowed_apart(ours_occlusion, aprime_occlusion));
      }
      // O2 within a render pixel of today's A' (designs\sss_aprime_cheaper.md 4).
      CHECK(std::abs(ours[0] - aprime[0]) <= to_screen && std::abs(ours[1] - aprime[1]) <= to_screen);
    }
  }

  // W8 extended at 50%: Softer Away from the Caster's penumbra over the reference caster's (case 4's rule) on W4's
  // wall behind a 0.29 m post: its median within 25%; CONTROL (the found caster one render pixel a sample) outside.
  {
    const Vector sun = {1.f, -0.24f, 0.03f};
    const std::array<Box, 2> pole = {{{.low = {-3.f, -2.f, 12.f}, .high = {-2.71f, 1.f, 12.03f}},
                                      {.low = {-4.f, -2.f, 12.03f}, .high = {8.f, 2.f, 12.5f}}}};
    const Share& share = SHARES[1];
    const Frame render = MakeFrame(share.width, share.height, pole, sun, 0.f, 0.f);
    sss::Settings settings = Defaults();
    settings.sun_disk = 2.f;
    const auto half_to_float = [](uint16_t half) {
      const uint32_t sign = (half & 0x8000u) << 16;
      const uint32_t exponent = (half >> 10) & 0x1Fu;
      const uint32_t mantissa = half & 0x3FFu;
      const uint32_t bits = (exponent == 0) ? sign : (sign | ((exponent + 112u) << 23) | (mantissa << 13));
      float value = 0.f;
      std::memcpy(&value, &bits, sizeof(value));
      return value;
    };
    const Vector to_sun = {-render.sun[0], -render.sun[1], -render.sun[2]};
    const float tangent = std::tan(1.f * 3.14159265f / 180.f);
    const auto median_ratio = [&](const std::vector<uint16_t>& penumbra) {
      std::vector<float> ratios;
      for (size_t pixel = 0; pixel < penumbra.size(); ++pixel) {
        float distance = 0.f;
        if (render.picture.hit[pixel] != 1 || Occluder(std::span(pole).first(1), render.picture.position[pixel], to_sun, 1e30f, &distance) < 0) continue;
        const float radius = half_to_float(penumbra[pixel]);
        if (radius <= 0.f) continue;
        const Vector& p = render.picture.position[pixel];
        const float view_z = (p[1] * render.camera.view[1][2]) + (p[2] * render.camera.view[2][2]);
        const float expected = distance * tangent * 0.5f * static_cast<float>(share.height) * render.camera.projection[1][1] / view_z;
        if (expected >= sss::internal::SOFT_RADIUS_2160 * static_cast<float>(share.height) / 2160.f) continue;
        ratios.push_back(radius / expected);
      }
      REQUIRE(ratios.size() > 20);
      std::ranges::nth_element(ratios, ratios.begin() + static_cast<ptrdiff_t>(ratios.size() / 2));
      return ratios[ratios.size() / 2];
    };
    const float ours = median_ratio(MarchAt(render, settings).penumbra);
    const float aprime = median_ratio(MarchAt(render, settings, aprime_form).penumbra);
    const float control = median_ratio(MarchAt(render, settings, {.find = shaders.render_find.Get()}).penumbra);
    std::printf("   W8 50%%: Softer's penumbra over the reference O2 %.3f, today's A' %.3f, CONTROL caster in render steps %.3f\n", ours, aprime, control);
    CHECK(ours > 0.75f && ours < 1.25f);
    CHECK(std::abs(ours - aprime) <= 0.02f);
    CHECK(control <= 0.75f || control >= 1.25f);
  }

  // W12 (apd_ O2, designs\sss_aprime_cheaper.md 4): at the exact shares O2 is today's A' (its positions and math; the
  // entry's distance from the grid thread, a few float ulps): on W1's scene, plain and with Softer, case 1's runs, pixels
  // apart by over 1 / 255 at most 0.1%, by over 0.25 at most 0.01% (one-surface ties). CONTROL 5 (must fail it): the grid
  // pass's rounds from the first, over entries later rounds still read.
  {
    const std::array<Box, 3> casters = {{BOX_SCENE[0], {.low = {-6.f, -2.f, 20.f}, .high = {6.f, 0.f, 22.f}}, {.low = {3.f, -2.f, 10.f}, .high = {3.2f, 2.f, 10.2f}}}};
    sss::Settings base_settings = Defaults();
    sss::Settings softer = Defaults();
    softer.sun_disk = 2.f;
    for (const auto& share : SHARES) {
      const Frame render = MakeFrame(share.width, share.height, casters, BOX_SUN);
      const auto pixels = static_cast<double>(share.width * share.height);
      for (const auto* settings : {&base_settings, &softer}) {
        std::vector<std::vector<uint8_t>> ours;
        std::vector<std::vector<uint8_t>> aprime;
        std::vector<std::vector<uint8_t>> ascending;
        for (int run = 0; run < RUNS; ++run) {
          ours.push_back(MarchAt(render, *settings).occlusion);
          aprime.push_back(MarchAt(render, *settings, aprime_form).occlusion);
          if (settings == &base_settings) ascending.push_back(MarchAt(render, *settings, {.plain = shaders.ascending.Get()}).occlusion);
        }
        const int over_one = apart_by(ours, aprime, 1);
        const int over_quarter = apart_by(ours, aprime, 63);
        std::printf("   W12 %s %s: pixels apart from today's A' by over 1 / 255 %d, over 0.25 %d (of %.0f)", share.name,
                    (settings == &base_settings) ? "plain" : "Softer", over_one, over_quarter, pixels);
        CHECK(over_one <= 0.001 * pixels && over_quarter <= 0.0001 * pixels);
        if (settings == &base_settings) {
          const int control = apart_by(ascending, aprime, 1);
          std::printf("; CONTROL rounds from the first %d over 1 / 255", control);
          CHECK(control > 0.001 * pixels);
        }
        std::printf("\n");
      }
    }
  }
  sss::internal::runtime.objects = {};
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: tests.exe <fc24dbce's plain shaders> [--time]\n");
    return 2;
  }
  const std::string mode = (argc > 2) ? argv[2] : "";
  if (mode != "--time-only") {
    TestRowsOff(argv[1]);
    TestAutoThickness();
    TestBanner();
    TestSofter();
    TestTorches();
    TestBattleLights(argv[1]);
    TestReplay();
    TestRowsTurnedOff();
    TestShogun2(argv[1]);
    TestShogun2Banners(argv[1]);
    TestScreenSteps(argv[1]);
    TestSameSurface(argv[1]);
  }
  if (mode == "--time" || mode == "--time-only") Time(argv[1]);
  std::printf(failures == 0 ? "PASS\n" : "%d FAILED\n", failures);
  return failures == 0 ? 0 : 1;
}
