// The screen-space shadows' extended march (twattila/screen_space_shadows.hpp, screen_space_lights.hpp and their shaders)
// on WARP, through the module's own Compute, OnThinDraw and OnLightDraw with Attila's embedded shaders (build32\
// twattila.include). tests.exe <fc24dbce's plain shaders, run.ps1's before_cso> [--time | --time-only | --block-skip |
// --block-skip-gpu | --block-skip-time-warp]
// The scenes: a ground plane 2 m below a camera looking 25 degrees down (60 degrees high), boxes on it, ray-traced on the
// CPU into the depth (and the G-buffer's colour, the banners' depth) and a reference shadow.
// 1. Rows off: the occlusion and the softened occlusion to the bit as fc24dbce's sss_depth / sss_setup / sss_bend / sss_blur
//    give them (sss_setup's dispatch list moved into sss_dispatch.hlsli; its arguments and parameters to the bit too), and
//    the extended march with nothing to do (no banners in view, Auto Thickness off) the plain march's to the bit, with
//    foliage marked (by the depth copies, Warhammer III's material byte, and Pharaoh's bytes with bit 128; CONTROL: its
//    water's bit 64 must differ). Pixels Bend's waves write twice in one dispatch vary between runs of the same shaders on WARP (both
//    sides', under 0.2% of the picture): 8 runs each in turns, a pixel apart where no value of one side's is the other's.
//    At 50% and 58% render scale (30 and 35 samples, fade-outs 14 and 19 after 16 hard ones) the extended march's counts
//    in fours: the plain march's with 32 and 36, 12 and 16, to the bit.
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
//    pick without the repeat test must differ.
// 10. Shogun 2's Light Through Banners and Coloured Shadows through its extended passes (sss_distance / _cascades from this
//     tree) on case 3's banner: its shadow keeps the row's share of the sun, its filter is its colour, the cascades' version
//     the same; rows off the game's sss01 to the bit. CONTROLS (must fail): the banners' depth or the filter's target unbound.
// 11. The block skip and the FarFade skip (bend_sss_gpu.hlsli): this tree's Bend passes against db99b716b's (run.ps1's
//     e2_cso) over the design's matrix (Lengths 60-1920, render scales 100-33%, hard and fade-out counts, Bend's flags,
//     contrasts, foliage, banners + Coloured + Softer, a wall past FarFade, the sun in view, behind and at the corners,
//     torches 1 / 8 / 32, grass, dense pillars, a low sun, the sun along a wall) to the bit: every pixel's writes
//     (BEND_SSS_TEST_WRITES, in any order), every output (apart only where written twice); Shogun 2's sss00 / sss01 /
//     sss_foliage* / sss_distance* at 120 and 3840 samples too. The counter builds (BEND_SSS_TEST_COUNTERS): no sample
//     within the shadow window in a skipped block. CONTROLS (must fail): Rev 0's sign-blind test, margins -1e-3 and
//     -0.25, every block skipped.
// 12. Resolution (Experimental): at Full every output as 352a9b785's (the commit before the row; run.ps1 builds these
//     tests on its header, SSS_ROW_BEFORE, whose --resolution-dump writes them), as case 1; 3/4, 1/2 and 1/4 against
//     Full, their pictures side by side in the build folder (TestResolution).
// --time: on the hardware GPU at 3840 x 2160, each row's GPU time (median of 30 frames; Resolution 3/4, 1/2 and 1/4 of
// rows off, Coloured Shadows and Softer too), torches near and far, the CPU time of a later light draw, and a battle's
// light draws with today's sss_lights and f6408aa1's. --time-only: that alone.
// --block-skip: case 11 alone on WARP. --block-skip-gpu: case 11 on the hardware GPU and its times (db99b716b's Bend
// passes against this tree's) at 3840 x 2160; --block-skip-time-warp: those rows' code on WARP at 480 x 270 (a smoke run).
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
#include <deque>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <mutex>
#include <numeric>
#include <random>
#include <ranges>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include "src/games/twattila/screen_space_lights.hpp"
#include "test/tw_particle_history/mock_reshade.hpp"

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

extern "C" __declspec(dllexport) const char* const NAME = "Screen-space shadows test";

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
};

static Camera MakeCamera(UINT width, UINT height) {
  const float tilt = 25.f * 3.14159265f / 180.f;
  const float c = std::cos(tilt);
  const float s = std::sin(tilt);
  const float tan_half_x = TAN_HALF_Y * static_cast<float>(width) / static_cast<float>(height);
  Camera camera = {.width = width, .height = height};
  camera.view = {{{1, 0, 0, 0}, {0, c, -s, 0}, {0, s, c, 0}, {0, 0, 0, 1}}};
  camera.projection = {{{1.f / tan_half_x, 0, 0, 0},
                        {0, 1.f / TAN_HALF_Y, 0, 0},
                        {0, 0, FAR_PLANE / (FAR_PLANE - NEAR_PLANE), 1},
                        {0, 0, -NEAR_PLANE * FAR_PLANE / (FAR_PLANE - NEAR_PLANE), 0}}};
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
      const float ndc_x = ((static_cast<float>(x) + 0.5f) / static_cast<float>(w) * 2.f) - 1.f;
      const float ndc_y = 1.f - ((static_cast<float>(y) + 0.5f) / static_cast<float>(h) * 2.f);
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

static Frame MakeFrame(UINT width, UINT height, std::span<const Box> boxes, const Vector& sun) {
  Frame frame = {.camera = MakeCamera(width, height), .sun = Normalize(sun)};
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

// Below 100% render scale: a frame of the display's size (`width` x `height`) whose depth, banners' depth and colour hold
// the scene rendered at `render_width` x `render_height` in their top-left (the rest far, the colour black), its camera
// the render's.
static Frame ScaledFrame(UINT width, UINT height, UINT render_width, UINT render_height, std::span<const Box> boxes, const Vector& sun) {
  const Frame render = MakeFrame(render_width, render_height, boxes, sun);
  Frame scaled = MakeFrame(width, height, {}, sun);
  const auto paste = [&](const Texture& from, DXGI_FORMAT format, auto rest) {
    std::vector<decltype(rest)> texels(width * height, rest);
    const auto rendered = ReadView<decltype(rest)>(from.srv.Get());
    for (UINT y = 0; y < render_height; ++y) {
      std::copy_n(rendered.begin() + (y * render_width), render_width, texels.begin() + (y * width));
    }
    return MakeTexture(width, height, format, texels.data(), width * 4);
  };
  scaled.depth = paste(render.depth, DXGI_FORMAT_R32_FLOAT, 1.f);
  scaled.thin = paste(render.thin, DXGI_FORMAT_R32_FLOAT, 1.f);
  scaled.colour = paste(render.colour, DXGI_FORMAT_R8G8B8A8_UNORM, 0xFF000000u);
  scaled.camera_constants = render.camera_constants;
  return scaled;
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
// a pixel is apart when no run of one gives a value a run of the other gave, varying when not every run of both gives
// the same. `same` false: the sides must differ (more than 20 pixels apart). Texels of any type (case 11: the penumbra's
// halves, the filter, the lights' bits); "over half" counts the stable ones over `half` (an R8's 127), more than `least`
// of them; under `most_varying` of the pixels (one in 500) may vary. `allowed` (case 11) instead: the pixels that may
// vary or be apart, every other one the same in all runs (a race shows as one varying): those Bend's waves wrote more
// than once (the writes builds' sums, the same on both sides to the bit: whichever write lands last, apart when the
// sides' groups take different times), or near the ones that varied (the smoothed occlusion's). Returns the varying
// pixels.
constexpr int RUNS = 8;
template <typename T>
static std::vector<uint8_t> CompareRuns(const char* what, const std::vector<std::vector<T>>& ours, const std::vector<std::vector<T>>& theirs,
                                        bool same = true, T half = 127, int least = 100, std::span<const uint8_t> allowed = {},
                                        double most_varying = 1.0 / 500) {
  std::vector<uint8_t> varying(ours[0].size());
  int apart = 0;
  int outside = 0;
  int over_half = 0;
  if (!allowed.empty()) REQUIRE(allowed.size() == ours[0].size());
  for (size_t pixel = 0; pixel < ours[0].size(); ++pixel) {
    bool stable = true;
    bool shared = false;
    for (int run = 0; run < RUNS; ++run) {
      stable = stable && ours[run][pixel] == ours[0][pixel] && theirs[run][pixel] == ours[0][pixel];
      for (int other = 0; other < RUNS; ++other) shared = shared || ours[run][pixel] == theirs[other][pixel];
    }
    // An apart pixel varies too.
    varying[pixel] = stable ? 0 : 1;
    apart += shared ? 0 : 1;
    outside += (!stable && !allowed.empty() && allowed[pixel] == 0) ? 1 : 0;
    over_half += (stable && ours[0][pixel] > half) ? 1 : 0;
  }
  const auto varied = std::ranges::count(varying, 1);
  std::printf("   %s: %d pixels over half the same, %d apart, %td varying between runs", what, over_half, apart, varied);
  if (!allowed.empty()) std::printf(" (%d of them where not allowed)", outside);
  std::printf("\n");
  if (!same) {
    CHECK(apart > 20);
  } else if (allowed.empty()) {
    CHECK(apart == 0);
    CHECK(over_half > least && static_cast<double>(varied) < static_cast<double>(ours[0].size()) * most_varying);
  } else {
    CHECK(outside == 0 && over_half > least);
  }
  return varying;
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

  // Below 100% (the render the top-left of the display's textures, 960 x 540): the sun's 60 samples are 30 at 50% and 35
  // at 58% (557 x 313), their fade-outs (32 after 16 hard ones) 14 and 19. The extended march (nothing to do) takes them
  // in fours: the plain march's with 32 and 36 samples, fade-outs 12 and 16 (which differs from the plain one's with the
  // odd counts).
  struct Scale {
    UINT width;
    UINT height;
    float samples;  // the plain march's Length for the rounded count at this scale
    float fade;
  };
  // The box, a wall behind it and a pole beside it.
  const std::array<Box, 3> casters = {{BOX_SCENE[0], {.low = {-6.f, -2.f, 20.f}, .high = {6.f, 0.f, 22.f}}, {.low = {3.f, -2.f, 10.f}, .high = {3.2f, 2.f, 10.2f}}}};
  for (const auto& scale : {Scale{480, 270, 64.f, 12.f}, Scale{557, 313, 62.f, 16.f}}) {
    const Frame scaled = ScaledFrame(960, 540, scale.width, scale.height, casters, BOX_SUN);  // its banners' depth all far
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
    for (int run = 0; run < RUNS; ++run) {
      odd_plain.push_back(ReadView<uint8_t>(RunFrame(scaled, odd)));
      rounded_plain.push_back(ReadView<uint8_t>(RunFrame(scaled, rounded)));
      rounded_extended.push_back(ReadView<uint8_t>(RunFrame(scaled, odd_extended, true)));
    }
    std::printf("   %u x %u of 960 x 540: the module's %.0f samples, fade-out %.0f\n", scale.width, scale.height, objects.uploaded.samples,
                objects.uploaded.fade_samples);
    CHECK(objects.uploaded.samples == std::round(60.f * static_cast<float>(scale.width) / 960.f) && std::fmod(objects.uploaded.samples, 4.f) != 0.f);
    CompareRuns("   extended march in fours against the plain one's with the counts in fours", rounded_extended, rounded_plain);
    CompareRuns("   the plain march's with the counts in fours against its own with the odd ones", rounded_plain, odd_plain, false);
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

// A half float's value (the penumbra's R16F; no infinities or NaNs there).
static float HalfToFloat(uint16_t half) {
  const uint32_t sign = (half & 0x8000u) << 16;
  const uint32_t exponent = (half >> 10) & 0x1Fu;
  const uint32_t mantissa = half & 0x3FFu;
  const uint32_t bits = (exponent == 0) ? sign : (sign | ((exponent + 112u) << 23) | (mantissa << 13));
  float value = 0.f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
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
    const float radius = HalfToFloat(penumbra[pixel]);
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
// if the second's first draw bound the lit bits. The sun's march with `settings` (case 11: its length, hard and fade-out
// samples).
template <typename Instance>
static bool LightFrames(const Frame& frame, std::span<const Instance> instances, uint32_t wanted, float range = 0.f, size_t per_draw = 0,
                        sss::Settings settings = Defaults()) {
  const auto buffer = MakeBuffer(static_cast<UINT>(instances.size_bytes()), D3D11_BIND_VERTEX_BUFFER, instances.data());
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

// shader_injection with `rows` (each a row's offset and value; the rest 0).
static ComPtr<ID3D11Buffer> ShogunInjection(const std::vector<std::pair<size_t, float>>& rows) {
  std::vector<float> floats((sizeof(shogun2::ShaderInjectData) + 15) / 16 * 4, 0.f);
  for (const auto& [offset, value] : rows) floats[offset / sizeof(float)] = value;
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
// into `output` (R8, cleared first), with `settings` at b13; the extended passes' banners' depth at t5, colour at t6, filter
// at u2 and split texture at u1 (Softer Away from the Caster's penumbra in its g) where given. The mask after it (none
// without `read`: --block-skip-gpu's times have no read-back in them).
struct ShogunExtras {
  ID3D11ShaderResourceView* thin = nullptr;
  ID3D11ShaderResourceView* colour = nullptr;
  ID3D11UnorderedAccessView* tint = nullptr;
  ID3D11UnorderedAccessView* split = nullptr;
};
static std::vector<uint8_t> RunShogunPass(const Frame& frame, const ShogunInputs& inputs, const Texture& output, ID3D11UnorderedAccessView* output_uav,
                                          ID3D11ComputeShader* shader, bool cascades, bool bend, ID3D11Buffer* settings, const ShogunExtras& extras = {},
                                          bool read = true) {
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
  context->CSSetUnorderedAccessViews(1, 1, &extras.split, nullptr);
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
  if (!read) return {};
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
  const auto defaults = ShogunInjection({});
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
    const auto control = ShogunInjection({{pass.control_row, 2.f}});
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
  const auto defaults = ShogunInjection({});
  const auto banners = ShogunInjection({{offsetof(shogun2::ShaderInjectData, sss_banner_light), 0.35f}});
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

// ---- 11. The block skip and the FarFade skip ------------------------------------------------------------------------

// Case 11's builds (run.ps1's e2_cso) of the module's Bend passes: `prefix` + sss_bend, sss_bend_extended (the light mode's
// too) and sss_bend_extended_find.
struct BendPasses {
  ComPtr<ID3D11ComputeShader> plain;
  ComPtr<ID3D11ComputeShader> extended;
  ComPtr<ID3D11ComputeShader> find;
};
static BendPasses LoadBendPasses(const std::filesystem::path& folder, const std::string& prefix) {
  return {.plain = LoadCso((folder / (prefix + "sss_bend.cso")).string()),
          .extended = LoadCso((folder / (prefix + "sss_bend_extended.cso")).string()),
          .find = LoadCso((folder / (prefix + "sss_bend_extended_find.cso")).string())};
}
// In the module's place (its device's objects, made first: they keep them until the device changes).
static void UseBendPasses(const BendPasses& passes) {
  auto& objects = sss::internal::runtime.objects;
  REQUIRE(sss::internal::Prepare(device.Get(), std::max(objects.width, 1u), std::max(objects.height, 1u)));
  objects.bend = passes.plain;
  objects.bend_extended[0] = passes.extended;
  objects.bend_extended[2] = passes.find;
}

// The sun's travel direction whose screen point (the light coordinate) is `ndc_x`, `ndc_y` (y up) of `camera`: in front of
// it (w > 0) or the point behind it (w < 0) that projects there.
static Vector SunAt(const Camera& camera, float ndc_x, float ndc_y, bool behind) {
  const float tan_half_x = TAN_HALF_Y * static_cast<float>(camera.width) / static_cast<float>(camera.height);
  const Vector view = {ndc_x * tan_half_x, ndc_y * TAN_HALF_Y, 1.f};
  Vector travel = {};
  for (int j = 0; j < 3; ++j) {
    travel[j] = ((view[0] * camera.view[j][0]) + (view[1] * camera.view[j][1]) + (view[2] * camera.view[j][2])) * (behind ? 1.f : -1.f);
  }
  return Normalize(travel);
}

// The lanes the shipped passes march by Bend's loop (fewer quads than BlockSkipQuads) on a case: not checked, all, none,
// the light mode's alone (its sun's march takes the block skip).
enum class BendLoop { UNCHECKED, ALL, NONE, LIGHTS };

// One of case 11's frames: its rows (Length, hard and fade-out samples among them), the render size below 100% (0: full),
// the banners' depth this frame, the foliage's depth pass, the lights (the light mode: settings.lights of them), the
// lanes in Bend's loop.
struct SkipCase {
  std::string name;
  const Frame* frame = nullptr;
  sss::Settings settings = Defaults();
  UINT render_width = 0;
  UINT render_height = 0;
  bool thin = false;
  const Foliage* foliage = nullptr;
  std::span<const LightInstance> lights;
  BendLoop bend_loop = BendLoop::UNCHECKED;
};

// What a frame of the module wrote: the occlusion, what the sun draws read where Softness or Softer smoothed it, the
// penumbra (Softer), the filter (Coloured Shadows), each pixel's lights' bits cleared (the light mode's mask inverted).
struct SkipOutputs {
  std::vector<uint8_t> occlusion;
  std::vector<uint8_t> smoothed;
  std::vector<uint16_t> penumbra;
  std::vector<uint32_t> tint;
  std::vector<uint32_t> shadowed;
};

static SkipOutputs RunSkipCase(const SkipCase& skip_case, const BendPasses& passes) {
  auto& runtime = sss::internal::runtime;
  auto& objects = runtime.objects;
  UseBendPasses(passes);
  twattila::frame_history::render_width = skip_case.render_width;
  twattila::frame_history::render_height = skip_case.render_height;
  SkipOutputs outputs;
  if (skip_case.lights.empty()) {
    ID3D11ShaderResourceView* read = RunFrame(*skip_case.frame, skip_case.settings, skip_case.thin, skip_case.foliage);
    if (read != objects.occlusion_srv.Get()) outputs.smoothed = ReadView<uint8_t>(read);
  } else {
    REQUIRE(LightFrames(*skip_case.frame, skip_case.lights, skip_case.settings.lights, 0.f, 0, skip_case.settings));
    outputs.shadowed = ReadView<uint32_t>(objects.lights.mask_uav.Get());
    for (uint32_t& bits : outputs.shadowed) bits = ~bits;
  }
  outputs.occlusion = ReadView<uint8_t>(objects.occlusion_srv.Get());
  if (skip_case.settings.sun_disk > 0.f) outputs.penumbra = ReadView<uint16_t>(objects.penumbra_srv.Get());
  if (runtime.tint_frame == runtime.frame) outputs.tint = ReadView<uint32_t>(objects.tint_srv.Get());
  twattila::frame_history::render_width = 0;
  twattila::frame_history::render_height = 0;
  return outputs;
}

// `ours` against `theirs` on a frame, RUNS times each in turns: every output (CompareRuns: more than 10 stable pixels
// not 0); `same` false: the occlusion must differ. `twice`: the pixels the sun's march wrote more than once a frame (the
// writes builds'), where alone the occlusion, the penumbra and the filter may vary or be apart; the smoothed occlusion
// only within its reach (sss_blur's taps Softness apart, sss_soft's search and disc within the larger of Softness and the
// penumbra's most) of a pixel whose occlusion or penumbra varied. The lights' bits (InterlockedAnd only clears them, in
// any order): the same in every run.
static void CompareSkipCase(const SkipCase& skip_case, const BendPasses& ours, const BendPasses& theirs, const char* against, bool same = true,
                            std::span<const uint8_t> twice = {}) {
  std::array<std::vector<SkipOutputs>, 2> sides;
  for (int run = 0; run < RUNS * 2; ++run) sides[run % 2].push_back(RunSkipCase(skip_case, ((run % 2) == 0) ? ours : theirs));
  const auto compare = [&](const char* output, auto member, std::span<const uint8_t> allowed) {
    using Texel = typename std::remove_cvref_t<decltype(sides[0][0].*member)>::value_type;
    if ((sides[0][0].*member).empty()) return std::vector<uint8_t>{};
    std::array<std::vector<std::vector<Texel>>, 2> texels;
    for (int side = 0; side < 2; ++side) {
      for (const auto& outputs : sides[side]) texels[side].push_back(outputs.*member);
    }
    return CompareRuns((skip_case.name + ", " + output + " " + against).c_str(), texels[0], texels[1], same, Texel{0}, 10, allowed);
  };
  std::vector<uint8_t> varied = compare("occlusion", &SkipOutputs::occlusion, twice);
  if (!same) return;
  const std::vector<uint8_t> penumbra = compare("penumbra", &SkipOutputs::penumbra, twice);
  for (size_t pixel = 0; pixel < penumbra.size(); ++pixel) varied[pixel] |= penumbra[pixel];
  if (!sides[0][0].smoothed.empty()) {
    const int width = static_cast<int>(skip_case.frame->camera.width);
    const int height = static_cast<int>(varied.size()) / width;
    const auto render_height = static_cast<float>((skip_case.render_height != 0) ? skip_case.render_height : skip_case.frame->camera.height);
    const int reach = static_cast<int>(std::ceil(std::max(skip_case.settings.softness, sss::internal::SOFT_RADIUS_2160 * render_height / 2160.f))) + 1;
    std::vector<uint8_t> near_varied(varied.size());
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        if (varied[(y * width) + x] == 0) continue;
        const int left = std::max(x - reach, 0);
        for (int row = std::max(y - reach, 0); row <= std::min(y + reach, height - 1); ++row) {
          std::fill_n(near_varied.begin() + (row * width) + left, std::min(x + reach, width - 1) - left + 1, uint8_t{1});
        }
      }
    }
    compare("smoothed", &SkipOutputs::smoothed, near_varied);
  }
  compare("filter", &SkipOutputs::tint, twice);
  compare("lights' shadows", &SkipOutputs::shadowed, {});
  if (!sides[0][0].shadowed.empty()) {
    const auto identical = std::ranges::count_if(sides[0], [&](const SkipOutputs& outputs) { return outputs.shadowed == sides[0][0].shadowed; })
                           + std::ranges::count_if(sides[1], [&](const SkipOutputs& outputs) { return outputs.shadowed == sides[0][0].shadowed; });
    std::printf("   %s: the lights' bits of %td of %d runs the same as the first's\n", skip_case.name.c_str(), identical, RUNS * 2);
    CHECK(identical == RUNS * 2);
  }
}

// The counter builds' counts (bend_sss_gpu.hlsli BEND_SSS_TEST_COUNTERS: a raw buffer at u7).
struct SkipCounts {
  uint64_t lanes = 0;          // lanes that marched
  uint64_t flipped = 0;        // of them with depth_scale <= 0
  uint64_t blocks = 0;         // the blocks of their main ranges
  uint64_t skipped = 0;        // of those skipped
  uint64_t false_skips = 0;    // samples within the shadow window (|offset| < 1) in skipped blocks
  uint64_t marched = 0;        // quads marched
  uint64_t quads = 0;          // quads in the main ranges
  uint64_t far_faded = 0;      // lanes FarFade left out
  uint64_t flipped_false = 0;  // false skips in lanes with depth_scale <= 0
  uint64_t near_misses = 0;    // samples in skipped blocks within 1e-3 past the window's edge (the false skips too)
  uint64_t flipped_inside = 0; // samples within the window in lanes with depth_scale <= 0 (any block)
  uint64_t bend_loop = 0;      // lanes Bend's loop marched (fewer quads than BlockSkipQuads)

  SkipCounts& operator+=(const SkipCounts& other) {
    lanes += other.lanes;
    flipped += other.flipped;
    blocks += other.blocks;
    skipped += other.skipped;
    false_skips += other.false_skips;
    marched += other.marched;
    quads += other.quads;
    far_faded += other.far_faded;
    flipped_false += other.flipped_false;
    near_misses += other.near_misses;
    flipped_inside += other.flipped_inside;
    bend_loop += other.bend_loop;
    return *this;
  }
};

// The test builds' raw buffer at u7: the counters (16 words), then the writes builds' 6 words per pixel of case 11's
// frames (480 x 270, their render sizes within), the sun's march's pixels, then the light mode's.
constexpr UINT TEST_PIXELS = 480 * 270;
constexpr UINT WRITE_WORDS = 6;
constexpr UINT TEST_WORDS = 16 + (WRITE_WORDS * 2 * TEST_PIXELS);
struct CounterBuffer {
  ComPtr<ID3D11Buffer> buffer;
  ComPtr<ID3D11UnorderedAccessView> uav;
};
static CounterBuffer MakeCounterBuffer() {
  CounterBuffer made = {.buffer = MakeBuffer(TEST_WORDS * 4, D3D11_BIND_UNORDERED_ACCESS, nullptr, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS)};
  const D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {.Format = DXGI_FORMAT_R32_TYPELESS, .ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
                                                 .Buffer = {.FirstElement = 0, .NumElements = TEST_WORDS, .Flags = D3D11_BUFFER_UAV_FLAG_RAW}};
  REQUIRE(SUCCEEDED(device->CreateUnorderedAccessView(made.buffer.Get(), &desc, made.uav.GetAddressOf())));
  return made;
}

// The words `work` (dispatches of a test build) left in the buffer, cleared and bound at u7 first (no pass of the module
// binds it).
template <typename Work>
static std::vector<uint32_t> TestWords(const CounterBuffer& counters, Work work) {
  constexpr std::array<UINT, 4> ZEROS = {};
  context->ClearUnorderedAccessViewUint(counters.uav.Get(), ZEROS.data());
  context->CSSetUnorderedAccessViews(7, 1, counters.uav.GetAddressOf(), nullptr);
  work();
  ID3D11UnorderedAccessView* none = nullptr;
  context->CSSetUnorderedAccessViews(7, 1, &none, nullptr);
  return ReadBuffer<uint32_t>(counters.buffer.Get());
}

// The counts of `work` (dispatches of a counter build).
template <typename Work>
static SkipCounts CountSkips(const CounterBuffer& counters, Work work) {
  const auto values = TestWords(counters, work);
  return {.lanes = values[0], .flipped = values[1], .blocks = values[2], .skipped = values[3], .false_skips = values[4], .marched = values[5],
          .quads = values[6], .far_faded = values[7], .flipped_false = values[8], .near_misses = values[9], .flipped_inside = values[10],
          .bend_loop = values[11]};
}

// The writes builds' sums (BEND_SSS_TEST_WRITES: each pixel's count of writes, the shadow terms' sum, least and most bits,
// the hits and the banner samples WriteExtended reads, the same in any order; the sun's march's and the light mode's
// apart) of `ours` against `theirs` over `frames` frames of the same march: every word the same (`same` false: some
// differ). Each pixel flagged where the sun's march wrote it more than once a frame.
static std::vector<uint8_t> CompareWrites(const std::string& what, const std::vector<uint32_t>& ours, const std::vector<uint32_t>& theirs,
                                          bool same = true, uint32_t frames = 1) {
  size_t differ = 0;
  std::array<size_t, 2> written = {};
  std::vector<uint8_t> twice(TEST_PIXELS);
  for (size_t slot = 0; slot < 2 * TEST_PIXELS; ++slot) {
    const size_t first = 16 + (WRITE_WORDS * slot);
    differ += std::equal(ours.begin() + first, ours.begin() + first + WRITE_WORDS, theirs.begin() + first) ? 0 : 1;
    written[slot / TEST_PIXELS] += ours[first];
    if (slot < TEST_PIXELS) twice[slot] = (ours[first] > frames) ? 1 : 0;
  }
  std::printf("   %s: %zu writes (and %zu of the light mode), %td pixels written more than once a frame, %zu pixels' sums differ\n", what.c_str(),
              written[0], written[1], std::ranges::count(twice, 1), differ);
  CHECK(written[0] > 0);
  CHECK(same == (differ == 0));
  return twice;
}

static void PrintCounts(const std::string& name, const SkipCounts& counts) {
  std::printf("   %s: %llu lanes (%llu with depth_scale <= 0, %llu samples of theirs in the window), %.1f%% of %llu blocks skipped (%llu "
              "samples in them within 1e-3 past the window), %.1f%% of the quads marched, %llu lanes FarFade left out, %llu in Bend's loop; "
              "FALSE SKIPS %llu (%llu with depth_scale <= 0)\n",
              name.c_str(), counts.lanes, counts.flipped, counts.flipped_inside,
              100.0 * static_cast<double>(counts.skipped) / static_cast<double>(std::max<uint64_t>(counts.blocks, 1)), counts.blocks, counts.near_misses,
              100.0 * static_cast<double>(counts.marched) / static_cast<double>(std::max<uint64_t>(counts.quads, 1)), counts.far_faded,
              counts.bend_loop, counts.false_skips, counts.flipped_false);
}

// Case 11's scenes on the ground 2 m below the camera. The box scene: a box, a wall behind it, a pole beside it, a red
// banner 3.5 x 4 m.
static std::vector<Box> SkipBoxScene() {
  return {BOX_SCENE[0], {.low = {-6.f, -2.f, 20.f}, .high = {6.f, 0.f, 22.f}}, {.low = {3.f, -2.f, 10.f}, .high = {3.2f, 2.f, 10.2f}},
          {.low = {-5.f, -1.f, 12.f}, .high = {-1.5f, 3.f, 12.05f}, .kind = Kind::THIN, .albedo = {0.8f, 0.1f, 0.1f}}};
}

// A dense field of pillars 0.3 m wide, 4 m high, 1.2 m apart from 6 to 40 m: depth edges in most of Bend's blocks (its
// worst case for the skip).
static std::vector<Box> SkipDenseScene() {
  std::mt19937 random(9);
  std::uniform_real_distribution<float> jitter(-0.3f, 0.3f);
  std::vector<Box> pillars;
  for (int row = 0; row < 14; ++row) {
    for (int column = 0; column < 20; ++column) {
      const float x = -12.f + (1.2f * static_cast<float>(column)) + jitter(random);
      const float z = 6.f + (2.5f * static_cast<float>(row)) + jitter(random);
      pillars.push_back({.low = {x, -2.f, z}, .high = {x + 0.3f, 2.f, z + 0.3f}});
    }
  }
  return pillars;
}

// The sun ahead, its screen point near the top of the view.
static const Vector SKIP_SUN_IN_VIEW = {0.1f, -0.05f, -1.f};

// 32 torches 0.6 m over the ground in front of the box scene's casters (their screen points on surfaces), their reach 1 m,
// 6 to 18 m away: sss_lights' budget (two screens of groups; Bend's dispatches over a light's rectangle are whole 64 x 64
// pixel tiles, 8 to 12 for each of these at 480 x 270, the budget 63) takes all of 4, all 32 fill it.
static std::vector<LightInstance> SkipTorches() {
  std::vector<LightInstance> torches;
  for (int index = 0; index < 32; ++index) {
    torches.push_back({.colour = {3.f, 1.5f, 0.4f, 0.f},
                       .light = {-6.f + (0.4f * static_cast<float>(index)), -1.4f, 6.f + (3.f * static_cast<float>(index % 5)), 1.f}});
  }
  return torches;
}

// 11. The block skip and the FarFade skip (bend_sss_gpu.hlsli BEND_SSS_BLOCK_SKIP) on the module's Bend passes: this tree's
// (as embedded, checked) against db99b716b's (run.ps1's e2_cso) over design 9.2's matrix. The writes builds (this tree's
// with both skips and without them, the latter db99b716b's bytes without the sums, run.ps1 checks) on each frame: every
// pixel's writes to the bit, whatever order the waves ran in, with the block skip on every ray (BEND_SSS_TEST_SKIP_QUADS
// 0) and from BlockSkipQuads (the shipped rule); then every output (CompareRuns), varying or apart only where a pixel was
// written more than once (whichever write lands last: the sides' groups take different times, a pixel can be one side's
// in all 8 runs and the other's in all of theirs); the counter builds on each frame: no sample within the shadow window
// in a skipped block (false skips 0) with either, blocks skipped on the box and grass scenes, FarFade's lanes left out
// past 60% of the far plane, lanes with depth_scale <= 0 reached, more lanes with more torches; with BlockSkipQuads each
// pass's lanes all in Bend's loop or none, as the case says (a quad below and at each K: plain and Light Through Banners
// 64, Coloured Shadows 24, Softer 40, the light mode 128). CONTROLS (must fail, every ray skipping):
// margin -1e-3 (false skips on the boundary frames: the sun along a wall), margin -0.25 (the writes builds' sums differ at
// Contrast 1), every block skipped (the occlusion apart; the sums differ). OBSERVATION (not a control): Rev 0's
// sign-blind block test on the sun-in-view and torches' frames can misjudge only samples within the window in lanes with
// depth_scale <= 0, counted: false skips unless there are none (those lanes sit within a pixel of the light's screen
// point; every sample of theirs lies past it, stored 1e10).
static void TestBlockSkip(const std::string& before_folder, D3D_DRIVER_TYPE driver) {
  std::printf("11. the block skip and the FarFade skip on %s\n", (driver == D3D_DRIVER_TYPE_WARP) ? "WARP" : "the hardware GPU");
  MakeDevice(driver);
  const auto folder = std::filesystem::path(before_folder).parent_path() / "e2_cso";
  const auto embedded = [&](std::span<const uint8_t> code, const char* name) {
    const auto file = ReadFile((folder / name).string());
    return std::ranges::equal(code, file);
  };
  const bool embeds = embedded(__sss_bend, "new_sss_bend.cso") && embedded(__sss_bend_extended, "new_sss_bend_extended.cso")
                      && embedded(__sss_bend_extended_find, "new_sss_bend_extended_find.cso")
                      && embedded(__sss_bend_extended_auto, "new_sss_bend_extended_auto.cso")
                      && embedded(__sss_bend_extended_auto_find, "new_sss_bend_extended_auto_find.cso");
  std::printf("   the module's embedded Bend passes this tree's: %s\n", embeds ? "yes" : "NO (rebuild twattila-shaders)");
  CHECK(embeds);
  const BendPasses fresh = LoadBendPasses(folder, "new_");
  const BendPasses old = LoadBendPasses(folder, "old_");
  const BendPasses counted = LoadBendPasses(folder, "count_");
  const BendPasses counted_shipped = LoadBendPasses(folder, "countk_");
  const CounterBuffer counters = MakeCounterBuffer();

  const std::vector<Box> box_scene = SkipBoxScene();
  std::vector<Box> far_scene = box_scene;
  far_scene.push_back({.low = {-3000.f, -2.f, 1500.f}, .high = {3000.f, 400.f, 1510.f}});  // past FarFade's 60%
  std::vector<Box> bush_scene = box_scene;
  bush_scene.push_back({.low = {2.f, -2.f, 7.f}, .high = {3.f, -1.f, 8.f}, .kind = Kind::FOLIAGE});
  std::vector<Box> grass_scene;  // blades about a render pixel wide at 7 m
  std::mt19937 random(5);
  std::uniform_real_distribution<float> unit(0.f, 1.f);
  for (int index = 0; index < 600; ++index) {
    const float x = -8.f + (16.f * unit(random));
    const float z = 5.f + (20.f * unit(random));
    grass_scene.push_back({.low = {x, -2.f, z}, .high = {x + 0.03f, -1.85f + (0.3f * unit(random)), z + 0.03f}});
  }
  const std::vector<Box> dense_scene = SkipDenseScene();
  const std::vector<LightInstance> torches = SkipTorches();
  const std::array<LightInstance, 1> near_torch = {{{.colour = {4.f, 2.f, 0.5f, 0.f}, .light = {0.4f, -1.3f, 1.8f, 4.f}}}};
  const Camera camera = MakeCamera(480, 270);
  const Vector low = {0.35f, -0.12f, 0.93f};  // a low sun behind: long shadows
  // The boundary: the sun along a wall's face (its travel in the plane x = -2.9): Bend's rays on it meet only the face, whose
  // depths, divided by their distance to the light's screen point, are the same (light rays are parallel there): offsets
  // +-1 within a few ulps (point-filtered: Edge Threshold 0).
  std::vector<Box> graze_scene = box_scene;
  graze_scene.push_back({.low = {-3.f, -2.f, 5.f}, .high = {-2.9f, 4.f, 40.f}});
  const Vector along_wall = {0.f, -0.5f, 0.8660254f};
  std::deque<Frame> frames;
  const auto frame = [&](Frame made) { return &frames.emplace_back(std::move(made)); };
  const Frame* box = frame(MakeFrame(480, 270, box_scene, BOX_SUN));
  const Frame* box_front = frame(MakeFrame(480, 270, box_scene, SKIP_SUN_IN_VIEW));
  const Frame* corner_front = frame(MakeFrame(480, 270, box_scene, SunAt(camera, -0.95f, 0.95f, false)));
  const Frame* corner_behind = frame(MakeFrame(480, 270, box_scene, SunAt(camera, 0.95f, -0.95f, true)));
  const Frame* low_sun = frame(MakeFrame(480, 270, box_scene, low));
  const Frame* boundary = frame(MakeFrame(480, 270, graze_scene, along_wall));
  const Frame* far_wall = frame(MakeFrame(480, 270, far_scene, BOX_SUN));
  const Frame* bush = frame(MakeFrame(480, 270, bush_scene, BOX_SUN));
  const Frame* grass = frame(MakeFrame(480, 270, grass_scene, BOX_SUN));
  const Frame* grass_front = frame(MakeFrame(480, 270, grass_scene, SKIP_SUN_IN_VIEW));
  const Frame* dense = frame(MakeFrame(480, 270, dense_scene, BOX_SUN));
  const Foliage foliage = {.before = box->depth, .after = bush->depth};
  // Render scales 77, 67, 58, 50 and 33% (Length 240: 185, 161, 139, 120 and 79 samples).
  struct Scale {
    const char* name;
    UINT width;
    UINT height;
  };
  constexpr std::array<Scale, 5> SCALES = {{{"77%", 370, 208}, {"67%", 322, 181}, {"58%", 278, 157}, {"50%", 240, 135}, {"33%", 158, 89}}};
  std::vector<const Frame*> scaled;
  for (const auto& scale : SCALES) scaled.push_back(frame(ScaledFrame(480, 270, scale.width, scale.height, box_scene, BOX_SUN)));
  const Frame* corner_58 = frame(ScaledFrame(480, 270, 278, 157, box_scene, SunAt(camera, -0.95f, 0.95f, false)));

  const auto rows = [](auto change) {
    sss::Settings settings = Defaults();
    change(settings);
    return settings;
  };
  const auto length = [&](float samples) { return rows([&](sss::Settings& settings) { settings.samples = samples; }); };
  const auto softer = [&](float samples) {
    return rows([&](sss::Settings& settings) {
      settings.samples = samples;
      settings.sun_disk = 0.5f;
      settings.thin_light = 0.35f;
      settings.coloured = true;
    });
  };
  const auto torch_rows = [&](uint32_t wanted, float samples) {
    return rows([&](sss::Settings& settings) {
      settings.samples = samples;
      settings.lights = wanted;
    });
  };
  std::vector<SkipCase> cases;
  for (const float samples : {60.f, 120.f, 240.f, 480.f, 960.f, 1920.f}) {
    cases.push_back({.name = "box, Length " + std::to_string(static_cast<int>(samples)), .frame = box, .settings = length(samples),
                     .bend_loop = (samples < 480.f) ? BendLoop::ALL : BendLoop::NONE});
  }
  for (size_t index = 0; index < SCALES.size(); ++index) {
    cases.push_back({.name = std::string("box at ") + SCALES[index].name, .frame = scaled[index], .settings = length(240.f),
                     .render_width = SCALES[index].width, .render_height = SCALES[index].height});
  }
  cases.push_back({.name = "box at 77%, Length 480", .frame = scaled[0], .settings = length(480.f), .render_width = 370, .render_height = 208});
  cases.push_back({.name = "box at 58%, Length 1920", .frame = scaled[2], .settings = length(1920.f), .render_width = 278, .render_height = 157});
  cases.push_back({.name = "box at 33%, Length 960", .frame = scaled[4], .settings = length(960.f), .render_width = 158, .render_height = 89});
  cases.push_back({.name = "box, Hard 0, Fade 4", .frame = box, .settings = rows([](sss::Settings& s) { s.hard_samples = 0.f; s.fade_samples = 4.f; })});
  cases.push_back({.name = "box, Hard 32", .frame = box, .settings = rows([](sss::Settings& s) { s.hard_samples = 32.f; })});
  cases.push_back({.name = "box, Fade 64", .frame = box, .settings = rows([](sss::Settings& s) { s.fade_samples = 64.f; })});
  cases.push_back({.name = "box, Length 60, Fade 64 (56 left)", .frame = box, .settings = rows([](sss::Settings& s) { s.samples = 60.f; s.fade_samples = 64.f; })});
  cases.push_back({.name = "box, Length 63, Hard 0, Fade 0", .frame = box, .settings = rows([](sss::Settings& s) { s.samples = 63.f; s.hard_samples = 0.f; s.fade_samples = 0.f; })});
  cases.push_back({.name = "box, Ignore Edges + Precision Offset", .frame = box, .settings = rows([](sss::Settings& s) { s.ignore_edges = true; s.precision_offset = true; })});
  cases.push_back({.name = "box, Along the Ray + Mirror Edges", .frame = box, .settings = rows([](sss::Settings& s) { s.sample_along_ray = true; s.mirror_edges = true; })});
  cases.push_back({.name = "box, Contrast 1", .frame = box, .settings = rows([](sss::Settings& s) { s.contrast = 1.f; })});
  cases.push_back({.name = "box, Contrast 2", .frame = box, .settings = rows([](sss::Settings& s) { s.contrast = 2.f; })});
  cases.push_back({.name = "box, Softness 2", .frame = box, .settings = rows([](sss::Settings& s) { s.softness = 2.f; })});
  cases.push_back({.name = "bush, Light Through Foliage 0.5", .frame = bush, .settings = rows([](sss::Settings& s) { s.foliage_light = 0.5f; }), .foliage = &foliage});
  cases.push_back({.name = "box, Light Through Banners", .frame = box, .settings = rows([](sss::Settings& s) { s.thin_light = 0.35f; }), .thin = true});
  cases.push_back({.name = "box, Banners + Coloured + Softer", .frame = box, .settings = softer(240.f), .thin = true});
  cases.push_back({.name = "box, Softer, Length 480", .frame = box, .settings = rows([](sss::Settings& s) { s.samples = 480.f; s.sun_disk = 0.5f; })});
  cases.push_back({.name = "box at 58%, Banners + Coloured + Softer", .frame = scaled[2], .settings = softer(240.f), .render_width = 278, .render_height = 157, .thin = true});
  cases.push_back({.name = "box, Light Through Banners, Length 1920", .frame = box, .settings = rows([](sss::Settings& s) { s.samples = 1920.f; s.thin_light = 0.35f; }), .thin = true});
  cases.push_back({.name = "far wall", .frame = far_wall, .settings = length(240.f)});
  cases.push_back({.name = "far wall, Banners + Coloured + Softer", .frame = far_wall, .settings = softer(240.f), .thin = true});
  cases.push_back({.name = "sun in view", .frame = box_front, .settings = length(240.f)});
  cases.push_back({.name = "sun in view, Banners + Coloured + Softer", .frame = box_front, .settings = softer(240.f), .thin = true});
  cases.push_back({.name = "sun at the top-left corner, in front", .frame = corner_front, .settings = length(240.f)});
  cases.push_back({.name = "sun at the bottom-right corner, behind", .frame = corner_behind, .settings = length(240.f)});
  cases.push_back({.name = "sun at the top-left corner at 58%", .frame = corner_58, .settings = length(240.f), .render_width = 278, .render_height = 157});
  cases.push_back({.name = "grass", .frame = grass, .settings = length(240.f)});
  cases.push_back({.name = "grass, Softer", .frame = grass, .settings = rows([](sss::Settings& s) { s.sun_disk = 0.5f; })});
  cases.push_back({.name = "grass, sun in view", .frame = grass_front, .settings = length(240.f)});
  cases.push_back({.name = "dense pillars", .frame = dense, .settings = length(240.f)});
  cases.push_back({.name = "dense pillars, Length 1920", .frame = dense, .settings = length(1920.f)});
  cases.push_back({.name = "dense pillars, Softer", .frame = dense, .settings = rows([](sss::Settings& s) { s.sun_disk = 0.5f; })});
  cases.push_back({.name = "low sun", .frame = low_sun, .settings = length(240.f)});
  cases.push_back({.name = "low sun, Contrast 1", .frame = low_sun, .settings = rows([](sss::Settings& s) { s.contrast = 1.f; })});
  cases.push_back({.name = "boundary (the sun along a wall)", .frame = boundary, .settings = rows([](sss::Settings& s) { s.edge_threshold = 0.f; })});
  cases.push_back({.name = "boundary (the sun along a wall), Softer", .frame = boundary,
                   .settings = rows([](sss::Settings& s) { s.edge_threshold = 0.f; s.sun_disk = 0.5f; })});
  cases.push_back({.name = "boundary (the sun along a wall), Contrast 1, Edge Threshold 0.02", .frame = boundary,
                   .settings = rows([](sss::Settings& s) { s.contrast = 1.f; })});
  for (const uint32_t wanted : {1u, 4u, 32u}) {
    cases.push_back({.name = "torches " + std::to_string(wanted), .frame = box, .settings = torch_rows(wanted, 240.f), .lights = torches});
  }
  // A torch whose reach takes in the camera: its dispatches over the whole screen (the budget's most for one light).
  cases.push_back({.name = "a torch 1.8 m away", .frame = box, .settings = torch_rows(1, 240.f), .lights = near_torch});
  cases.push_back({.name = "torches 32, Length 1920", .frame = box, .settings = torch_rows(32, 1920.f), .lights = torches});
  cases.push_back({.name = "torches 32 at 58%", .frame = scaled[2], .settings = torch_rows(32, 240.f), .render_width = 278, .render_height = 157, .lights = torches});
  cases.push_back({.name = "torches 32, sun in view", .frame = box_front, .settings = torch_rows(32, 240.f), .lights = torches});
  // A quad below each BlockSkipQuads and at it (quads = (Length - 12 + 3) / 4 at 100%, Hard 4, Fade 8).
  for (const bool below : {true, false}) {
    const auto at = [&](float samples, int quads) {
      return ", Length " + std::to_string(static_cast<int>(samples)) + " (" + std::to_string(quads) + " quads: " + (below ? "Bend's loop)" : "the block skip)");
    };
    const BendLoop sun_loop = below ? BendLoop::ALL : BendLoop::NONE;
    const float plain = below ? 264.f : 268.f;
    cases.push_back({.name = "box" + at(plain, below ? 63 : 64), .frame = box, .settings = length(plain), .bend_loop = sun_loop});
    cases.push_back({.name = "box, Light Through Banners" + at(plain, below ? 63 : 64), .frame = box,
                     .settings = rows([&](sss::Settings& s) { s.samples = plain; s.thin_light = 0.35f; }), .thin = true, .bend_loop = sun_loop});
    const float coloured = below ? 104.f : 108.f;
    cases.push_back({.name = "box, Banners + Coloured" + at(coloured, below ? 23 : 24), .frame = box,
                     .settings = rows([&](sss::Settings& s) { s.samples = coloured; s.thin_light = 0.35f; s.coloured = true; }), .thin = true,
                     .bend_loop = sun_loop});
    const float soft = below ? 168.f : 172.f;
    cases.push_back({.name = "box, Softer" + at(soft, below ? 39 : 40), .frame = box,
                     .settings = rows([&](sss::Settings& s) { s.samples = soft; s.sun_disk = 0.5f; }), .bend_loop = sun_loop});
    const float lit = below ? 520.f : 524.f;
    cases.push_back({.name = "torches 32" + at(lit, below ? 127 : 128), .frame = box, .settings = torch_rows(32, lit), .lights = torches,
                     .bend_loop = below ? BendLoop::LIGHTS : BendLoop::NONE});
  }

  const BendPasses written = LoadBendPasses(folder, "wnew_");
  const BendPasses written_shipped = LoadBendPasses(folder, "wk_");
  const BendPasses written_before = LoadBendPasses(folder, "wref_");
  // A frame marches the lights the frame before drew (LightFrames' first those of the case before): the sums after a
  // frame of the case (the light mode's: LightFrames' two, each with the sun's march).
  const auto writes = [&](const SkipCase& skip_case, const BendPasses& passes) {
    RunSkipCase(skip_case, passes);
    return TestWords(counters, [&] { RunSkipCase(skip_case, passes); });
  };
  SkipCounts total;
  SkipCounts total_shipped;
  std::vector<uint64_t> torch_lanes;  // torches 1, 4 and 32's
  for (const auto& skip_case : cases) {
    const uint32_t frames = skip_case.lights.empty() ? 1u : 2u;
    const std::vector<uint32_t> reference = writes(skip_case, written_before);
    const auto twice = CompareWrites(skip_case.name + ", writes against db99b716b's", writes(skip_case, written), reference, true, frames);
    CompareWrites(skip_case.name + ", writes (BlockSkipQuads) against db99b716b's", writes(skip_case, written_shipped), reference, true, frames);
    CompareSkipCase(skip_case, fresh, old, "against db99b716b's", true, twice);
    const SkipCounts counts = CountSkips(counters, [&] { RunSkipCase(skip_case, counted); });
    PrintCounts(skip_case.name, counts);
    CHECK(counts.false_skips == 0);
    if (skip_case.name == "box, Length 240" || skip_case.name == "grass") CHECK(counts.skipped > 0);
    if (skip_case.name.starts_with("far wall")) CHECK(counts.far_faded > 0);
    if (skip_case.name == "torches 1" || skip_case.name == "torches 4" || skip_case.name == "torches 32") torch_lanes.push_back(counts.lanes);
    total += counts;
    const SkipCounts shipped = CountSkips(counters, [&] { RunSkipCase(skip_case, counted_shipped); });
    PrintCounts(skip_case.name + " (BlockSkipQuads)", shipped);
    CHECK(shipped.false_skips == 0 && shipped.lanes == counts.lanes);
    switch (skip_case.bend_loop) {
      case BendLoop::ALL:     CHECK(shipped.bend_loop == shipped.lanes && shipped.skipped == 0); break;
      case BendLoop::NONE:    CHECK(shipped.bend_loop == 0 && shipped.skipped > 0); break;
      case BendLoop::LIGHTS:  CHECK(shipped.bend_loop > 0 && shipped.bend_loop < shipped.lanes && shipped.skipped > 0); break;
      case BendLoop::UNCHECKED:
        if (skip_case.lights.empty()) CHECK(shipped.bend_loop == 0 || shipped.bend_loop == shipped.lanes);
        break;
    }
    total_shipped += shipped;
  }
  PrintCounts("ALL", total);
  CHECK(total.false_skips == 0 && total.flipped > 0 && total.skipped > 0 && total.far_faded > 0);
  PrintCounts("ALL (BlockSkipQuads)", total_shipped);
  CHECK(total_shipped.false_skips == 0 && total_shipped.skipped > 0 && total_shipped.bend_loop > 0 && total_shipped.bend_loop < total_shipped.lanes);
  CHECK(torch_lanes.size() == 3 && torch_lanes[0] < torch_lanes[1] && torch_lanes[1] < torch_lanes[2]);

  // CONTROLS.
  const auto control_counts = [&](const BendPasses& passes, std::initializer_list<size_t> indexes) {
    SkipCounts sum;
    for (const size_t index : indexes) sum += CountSkips(counters, [&] { RunSkipCase(cases[index], passes); });
    return sum;
  };
  const auto find_case = [&](const char* name) {
    return static_cast<size_t>(std::ranges::find(cases, std::string(name), &SkipCase::name) - cases.begin());
  };
  // OBSERVATION, Rev 0's sign-blind test: lanes with depth_scale <= 0 reached (within a pixel of the light's screen point,
  // its rays past it); a false skip needs a sample of theirs within the window, counted in any block.
  const SkipCounts signed_counts = control_counts(LoadBendPasses(folder, "signed_"), {find_case("sun in view"), find_case("torches 32, sun in view")});
  PrintCounts("OBSERVATION (not a control) Rev 0's sign-blind test, sun in view + 32 torches", signed_counts);
  CHECK(signed_counts.flipped > 0);
  CHECK(signed_counts.false_skips > 0 || signed_counts.flipped_inside == 0);
  const BendPasses margin3 = LoadBendPasses(folder, "margin3_");
  const SkipCounts margin_counts = control_counts(margin3, {find_case("boundary (the sun along a wall)")});
  const SkipCounts margin_find_counts = control_counts(margin3, {find_case("boundary (the sun along a wall), Softer")});
  PrintCounts("CONTROL margin -1e-3, boundary", margin_counts);
  PrintCounts("CONTROL margin -1e-3, boundary, Softer", margin_find_counts);
  CHECK(margin_counts.false_skips > 0 && margin_find_counts.false_skips > 0);
  const SkipCase& margin_case = cases[find_case("low sun, Contrast 1")];
  CompareWrites(margin_case.name + ", writes CONTROL margin -0.25 against db99b716b's", writes(margin_case, LoadBendPasses(folder, "wmargin25_")),
                writes(margin_case, written_before), false);
  CompareSkipCase(cases[find_case("box, Length 240")], LoadBendPasses(folder, "skipall_"), old, "CONTROL every block skipped against db99b716b's", false);
  const SkipCase& skip_all_case = cases[find_case("box, Banners + Coloured + Softer")];
  CompareWrites(skip_all_case.name + ", writes CONTROL every block skipped against db99b716b's", writes(skip_all_case, LoadBendPasses(folder, "wskipall_")),
                writes(skip_all_case, written_before), false);
  // The module's own passes back.
  sss::internal::runtime.objects = {};
}

// 11 on Shogun 2's sun shadow passes (sss.hlsli, SAMPLE_COUNT 3840): sss00 / sss01, Light Through Foliage's sss_foliage(_cascades)
// (the depth's foliage pixels negative; light casters: blocks of 32 at 3840) and Softer Away from the Caster's
// sss_distance(_cascades) (with the banner, its filter and the split texture's penumbra), 120, 240, 480 and 3840 samples,
// this tree's against db99b716b's to the bit: the writes builds' sums (the block skip on every ray and from
// BlockSkipQuads), then the mask, the filter and the split texture (varying or apart only where written twice); the
// counter builds: no false skip, with BlockSkipQuads every lane in Bend's loop below it and none from it.
static void TestBlockSkipShogun2(const std::string& before_folder, D3D_DRIVER_TYPE driver) {
  std::printf("11. the block skip on Shogun 2's passes on %s\n", (driver == D3D_DRIVER_TYPE_WARP) ? "WARP" : "the hardware GPU");
  MakeDevice(driver);
  const auto folder = std::filesystem::path(before_folder).parent_path() / "e2_cso";
  std::vector<Box> boxes = SkipBoxScene();
  boxes.push_back({.low = {2.f, -2.f, 7.f}, .high = {3.f, -1.f, 8.f}, .kind = Kind::FOLIAGE});
  const Frame frame = MakeFrame(480, 270, boxes, BOX_SUN);
  const ShogunInputs inputs = MakeShogunInputs(frame, false);
  const UINT w = frame.camera.width;
  const UINT h = frame.camera.height;
  // Light Through Foliage's depth: the foliage's pixels negative.
  Frame foliage = frame;
  std::vector<float> depth = frame.picture.depth;
  for (size_t pixel = 0; pixel < depth.size(); ++pixel) {
    const int hit = frame.picture.hit[pixel];
    if (hit >= 0 && boxes[hit].kind == Kind::FOLIAGE) depth[pixel] = -depth[pixel];
  }
  foliage.depth = MakeTexture(w, h, DXGI_FORMAT_R32_FLOAT, depth.data(), w * 4);
  const Texture output = MakeTexture(w, h, DXGI_FORMAT_R8_UNORM, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS);
  const Texture tint = MakeTexture(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS);
  const Texture split = MakeTexture(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS);
  ComPtr<ID3D11UnorderedAccessView> output_uav;
  ComPtr<ID3D11UnorderedAccessView> tint_uav;
  ComPtr<ID3D11UnorderedAccessView> split_uav;
  REQUIRE(SUCCEEDED(device->CreateUnorderedAccessView(output.texture.Get(), nullptr, output_uav.GetAddressOf())));
  REQUIRE(SUCCEEDED(device->CreateUnorderedAccessView(tint.texture.Get(), nullptr, tint_uav.GetAddressOf())));
  REQUIRE(SUCCEEDED(device->CreateUnorderedAccessView(split.texture.Get(), nullptr, split_uav.GetAddressOf())));
  const CounterBuffer counters = MakeCounterBuffer();
  struct Pass {
    const char* name;
    bool cascades;
    bool foliage;
    bool distance;
  };
  constexpr std::array<Pass, 6> PASSES = {{{"s2_sss00", true, false, false},
                                           {"s2_sss01", false, false, false},
                                           {"s2_sss_foliage", false, true, false},
                                           {"s2_sss_foliage_cascades", true, true, false},
                                           {"s2_sss_distance", false, true, true},
                                           {"s2_sss_distance_cascades", true, true, true}}};
  using Data = shogun2::ShaderInjectData;
  SkipCounts total;
  SkipCounts total_shipped;
  for (const auto& pass : PASSES) {
    const auto old = LoadCso((folder / (std::string("old_") + pass.name + ".cso")).string());
    const auto fresh = LoadCso((folder / (std::string("new_") + pass.name + ".cso")).string());
    const auto counted = LoadCso((folder / (std::string("count_") + pass.name + ".cso")).string());
    const auto counted_shipped = LoadCso((folder / (std::string("countk_") + pass.name + ".cso")).string());
    const auto written = LoadCso((folder / (std::string("wnew_") + pass.name + ".cso")).string());
    const auto written_shipped = LoadCso((folder / (std::string("wk_") + pass.name + ".cso")).string());
    const auto written_before = LoadCso((folder / (std::string("wref_") + pass.name + ".cso")).string());
    const Frame& input = pass.foliage ? foliage : frame;
    const ShogunExtras extras = pass.distance ? ShogunExtras{.thin = frame.thin.srv.Get(), .colour = frame.colour.srv.Get(), .tint = tint_uav.Get(), .split = split_uav.Get()}
                                              : ShogunExtras{};
    // 3840 samples (Shogun 2's row at 5), 120 with Hard 4, Fade 4 and Contrast 2, 240 and 480 (rows 1 and 2; Hard 8, Fade
    // 16): their quads 954, 28, 54 and 114, the block skip's from 64 (sss_distance's, banners and their filter bound:
    // FindThin's 24).
    for (const int row : {0, 1, 2, 3}) {
      std::vector<std::pair<size_t, float>> rows;
      if (row == 1) {
        rows = {{offsetof(Data, sss_hard_samples), 4.f}, {offsetof(Data, sss_fade_samples), 4.f}, {offsetof(Data, sss_contrast), 2.f}};
      } else {
        rows = {{offsetof(Data, sss_samples), (row == 0) ? 5.f : static_cast<float>(row - 1)}};
      }
      constexpr std::array<uint32_t, 4> QUADS = {954, 28, 54, 114};
      if (pass.foliage) rows.emplace_back(offsetof(Data, sss_foliage_light), 0.5f);
      if (pass.distance) {
        rows.emplace_back(offsetof(Data, sss_banner_light), 0.35f);
        rows.emplace_back(offsetof(Data, sss_distance_softness), 2.f);
      }
      const auto settings = ShogunInjection(rows);
      const auto run = [&](ID3D11ComputeShader* shader, std::vector<uint32_t>* filter, std::vector<uint32_t>* penumbra) {
        constexpr std::array<UINT, 4> CLEAR = {};
        context->ClearUnorderedAccessViewUint(tint_uav.Get(), CLEAR.data());
        context->ClearUnorderedAccessViewUint(split_uav.Get(), CLEAR.data());
        auto mask = RunShogunPass(input, inputs, output, output_uav.Get(), shader, pass.cascades, true, settings.Get(), extras);
        if (pass.distance) {
          *filter = ReadTexture<uint32_t>(tint.texture.Get());
          *penumbra = ReadTexture<uint32_t>(split.texture.Get());
        }
        return mask;
      };
      std::array<std::vector<std::vector<uint8_t>>, 2> masks;
      std::array<std::vector<std::vector<uint32_t>>, 2> filters;
      std::array<std::vector<std::vector<uint32_t>>, 2> penumbras;
      for (int round = 0; round < RUNS * 2; ++round) {
        const int side = round % 2;
        std::vector<uint32_t> filter;
        std::vector<uint32_t> penumbra;
        masks[side].push_back(run((side == 0) ? fresh.Get() : old.Get(), &filter, &penumbra));
        filters[side].push_back(std::move(filter));
        penumbras[side].push_back(std::move(penumbra));
      }
      constexpr std::array<const char*, 4> ROW_NAMES = {", 3840 samples", ", 120 samples, Hard 4, Fade 4, Contrast 2", ", 240 samples", ", 480 samples"};
      const std::string name = std::string(pass.name) + ROW_NAMES[row];
      std::vector<uint32_t> ignored;
      // RunShogunPass ends with ClearState: the test buffer bound again first.
      const auto bound = [&](ID3D11ComputeShader* shader) {
        return [&, shader] {
          context->CSSetUnorderedAccessViews(7, 1, counters.uav.GetAddressOf(), nullptr);
          run(shader, &ignored, &ignored);
        };
      };
      const std::vector<uint32_t> reference = TestWords(counters, bound(written_before.Get()));
      const auto twice = CompareWrites(name + ", writes against db99b716b's", TestWords(counters, bound(written.Get())), reference);
      CompareWrites(name + ", writes (BlockSkipQuads) against db99b716b's", TestWords(counters, bound(written_shipped.Get())), reference);
      CompareRuns((name + ", mask against db99b716b's").c_str(), masks[0], masks[1], true, uint8_t{127}, 100, twice);
      if (pass.distance) {
        CompareRuns((name + ", filter against db99b716b's").c_str(), filters[0], filters[1], true, 0u, 10, twice);
        CompareRuns((name + ", split texture (penumbra) against db99b716b's").c_str(), penumbras[0], penumbras[1], true, 0u, 10, twice);
      }
      const SkipCounts counts = CountSkips(counters, bound(counted.Get()));
      PrintCounts(name, counts);
      CHECK(counts.false_skips == 0 && counts.lanes > 0);
      total += counts;
      const SkipCounts shipped = CountSkips(counters, bound(counted_shipped.Get()));
      PrintCounts(name + " (BlockSkipQuads)", shipped);
      const bool bend_loop = QUADS[row] < (pass.distance ? 24u : 64u);
      CHECK(shipped.false_skips == 0 && shipped.lanes == counts.lanes && shipped.bend_loop == (bend_loop ? shipped.lanes : 0));
      total_shipped += shipped;
    }
  }
  PrintCounts("Shogun 2 ALL", total);
  CHECK(total.false_skips == 0 && total.skipped > 0);
  PrintCounts("Shogun 2 ALL (BlockSkipQuads)", total_shipped);
  CHECK(total_shipped.false_skips == 0 && total_shipped.skipped > 0 && total_shipped.bend_loop > 0);
}

// Case 12's outputs at Full, each RUNS times: FullOutputs' order; `texel` its texels' bytes (0: a buffer, the same bytes in
// every run).
struct FullOutput {
  const char* name;
  size_t texel;
};
static constexpr std::array<FullOutput, 9> FULL_OUTPUTS = {{
    {"rows off: the occlusion", 1},
    {"rows off: sss_setup's parameters", 0},
    {"rows off: sss_setup's arguments", 0},
    {"Softness 2 px: the softened occlusion", 1},
    {"banner, Light Through Banners + Coloured Shadows + Softer: the softened occlusion", 1},
    {"banner: the occlusion", 1},
    {"banner: the penumbra", 2},
    {"banner: the filter", 4},
    {"Torch and Fire Shadows 4 beside the box: the lit bits", 4},
}};

// Case 12's Full frames, in this build and in run.ps1's on 352a9b785's header (SSS_ROW_BEFORE, --resolution-dump): the box
// scene with the rows off and Softness 2 px, case 3's banner with Light Through Banners, Coloured Shadows and Softer Away
// from the Caster, a torch beside the box; output i's run r at i x RUNS + r, its bytes.
static std::vector<std::vector<uint8_t>> FullOutputs() {
  MakeDevice(D3D_DRIVER_TYPE_WARP);
  auto& objects = sss::internal::runtime.objects;
  sss::internal::runtime.albedo_power = 2.f;
  const Frame box = MakeFrame(480, 270, BOX_SCENE, BOX_SUN);
  const std::array<Box, 1> banner_scene = {{{.low = {-1.f, -0.6f, 12.f}, .high = {1.f, 1.4f, 12.05f}, .kind = Kind::THIN, .albedo = {0.8f, 0.1f, 0.1f}}}};
  const Frame banner = MakeFrame(480, 270, banner_scene, {0.2f, -0.6f, -0.77f});
  sss::Settings softness = Defaults();
  softness.softness = 2.f;
  sss::Settings extended = Defaults();
  extended.thin_light = 0.35f;
  extended.coloured = true;
  extended.sun_disk = 2.f;
  const LightInstance torch = {.colour = {4.f, 2.f, 0.5f, 0.f}, .light = {2.f, -1.2f, 10.f, 6.f}};
  const auto bytes = [](const auto& texels) {
    std::vector<uint8_t> copied(texels.size() * sizeof(texels[0]));
    std::memcpy(copied.data(), texels.data(), copied.size());
    return copied;
  };
  std::vector<std::vector<uint8_t>> outputs(FULL_OUTPUTS.size() * RUNS);
  for (int run = 0; run < RUNS; ++run) {
    size_t output = 0;
    const auto keep = [&](std::vector<uint8_t> kept) { outputs[(output++ * RUNS) + run] = std::move(kept); };
    keep(ReadView<uint8_t>(RunFrame(box, Defaults())));
    keep(ReadBuffer<uint8_t>(objects.parameters.Get()));
    keep(ReadBuffer<uint8_t>(objects.arguments.Get()));
    keep(ReadView<uint8_t>(RunFrame(box, softness)));
    keep(ReadView<uint8_t>(RunFrame(banner, extended, true)));
    keep(ReadView<uint8_t>(objects.occlusion_srv.Get()));
    keep(bytes(ReadView<uint16_t>(objects.penumbra_srv.Get())));
    keep(bytes(ReadView<uint32_t>(objects.tint_srv.Get())));
    REQUIRE(LightFrames(box, std::span(&torch, 1), 4));
    keep(bytes(ReadView<uint32_t>(objects.lights.mask_uav.Get())));
    REQUIRE(output == FULL_OUTPUTS.size());
  }
  return outputs;
}

#ifndef SSS_ROW_BEFORE
// Pictures (each `width` x `height`, what the sun draws read: 255 in the shadow) side by side as a PGM, the sun's light white.
static void WriteStrip(const std::filesystem::path& path, UINT width, UINT height, std::span<const std::vector<uint8_t>> pictures) {
  std::ofstream file(path, std::ios::binary);
  file << "P5\n" << (width * pictures.size()) << ' ' << height << "\n255\n";
  for (UINT y = 0; y < height; ++y) {
    for (const auto& picture : pictures) {
      for (UINT x = 0; x < width; ++x) file.put(static_cast<char>(255 - picture[(y * width) + x]));
    }
  }
  REQUIRE(file.good());
}

// 12. Resolution (Experimental): at Full the module's outputs (FullOutputs) as 352a9b785's (the commit before the row:
//     run.ps1's build of these tests on its header dumped them); 3/4, 1/2 and 1/4 against Full: the march's size the
//     render's share rounded up and its samples that share, ground pixels wrong against the reference (under Full's
//     + 25%), the banner's filter (no red, green over 0.5) and its occlusion (Full's within 25%), the penumbra in render
//     pixels (Full's within 30%: median), the torches' bits Full's (their march stays there), Debug View 2's dispatch list
//     Full's, Full (sss::Update) letting the low textures go. CONTROLS (must differ): 1/2's occlusion and its dispatch
//     list without Debug View against Full's. The pictures side by side (Full, 3/4, 1/2, 1/4) into the test's build
//     folder: resolution_box.pgm, _banner.pgm, _pole.pgm.
static void TestResolution(const std::string& before_folder) {
  std::printf("12. Resolution\n");
  const auto dumped = ReadFile(before_folder + "\\resolution_full.bin");
  REQUIRE(!dumped.empty());
  std::vector<std::vector<uint8_t>> theirs;
  for (size_t at = 0; at < dumped.size();) {
    uint64_t size = 0;
    REQUIRE(at + sizeof(size) <= dumped.size());
    std::memcpy(&size, dumped.data() + at, sizeof(size));
    at += sizeof(size);
    REQUIRE(at + size <= dumped.size());
    theirs.emplace_back(dumped.begin() + static_cast<ptrdiff_t>(at), dumped.begin() + static_cast<ptrdiff_t>(at + size));
    at += size;
  }
  const auto ours = FullOutputs();
  REQUIRE(theirs.size() == ours.size());
  // Output `index` on both sides as `T` texels, compared as case 1 (`half`: CompareRuns'), up to one in 250 pixels varying
  // (the same code on both sides; Softer's penumbra on the banner varies in about one in 500).
  const auto compare = [&]<typename T>(size_t index, T half) {
    std::array<std::vector<std::vector<T>>, 2> sides;
    for (int run = 0; run < RUNS; ++run) {
      for (size_t side = 0; side < 2; ++side) {
        const auto& kept = ((side == 0) ? ours : theirs)[(index * RUNS) + run];
        auto& texels = sides[side].emplace_back(kept.size() / sizeof(T));
        std::memcpy(texels.data(), kept.data(), kept.size());
      }
    }
    CompareRuns(FULL_OUTPUTS[index].name, sides[0], sides[1], true, half, 100, {}, 1.0 / 250);
  };
  std::printf("   Full against 352a9b785's:\n");
  for (size_t index = 0; index < FULL_OUTPUTS.size(); ++index) {
    if (FULL_OUTPUTS[index].texel == 0) {
      bool same = true;
      for (int run = 0; run < RUNS; ++run) same = same && ours[(index * RUNS) + run] == theirs[(index * RUNS) + run];
      std::printf("   %s: %s\n", FULL_OUTPUTS[index].name, same ? "the same bytes in every run" : "APART");
      CHECK(same);
    } else if (FULL_OUTPUTS[index].texel == 1) {
      compare(index, uint8_t{127});
    } else if (FULL_OUTPUTS[index].texel == 2) {
      compare(index, uint16_t{0});
    } else {
      compare(index, uint32_t{0});
    }
  }

  constexpr std::array<float, 4> RESOLUTIONS = {1.f, 0.75f, 0.5f, 0.25f};
  constexpr std::array<const char*, 4> LABELS = {"Full", "3/4", "1/2", "1/4"};
  const auto folder = std::filesystem::path(before_folder).parent_path();
  MakeDevice(D3D_DRIVER_TYPE_WARP);
  auto& objects = sss::internal::runtime.objects;
  sss::internal::runtime.albedo_power = 2.f;
  const auto at = [](sss::Settings settings, float resolution) {
    settings.resolution = resolution;
    return settings;
  };
  // The box, rows off.
  const Frame box = MakeFrame(480, 270, BOX_SCENE, BOX_SUN);
  std::array<std::vector<uint8_t>, 4> plain;
  std::array<int, 4> wrong = {};
  for (size_t index = 0; index < RESOLUTIONS.size(); ++index) {
    plain[index] = ReadView<uint8_t>(RunFrame(box, at(Defaults(), RESOLUTIONS[index])));
    wrong[index] = WrongPixels(box, BOX_SCENE, plain[index]);
    const auto width = static_cast<UINT>(std::ceil(480.f * RESOLUTIONS[index]));
    const auto height = static_cast<UINT>(std::ceil(270.f * RESOLUTIONS[index]));
    const float samples = std::max(std::round(Defaults().samples * RESOLUTIONS[index]), Defaults().hard_samples + 4.f);
    int apart = 0;
    for (size_t pixel = 0; pixel < plain[index].size(); ++pixel) apart += (std::abs(plain[index][pixel] - plain[0][pixel]) > 64) ? 1 : 0;
    std::printf("   the box, rows off, %s: march %u x %u, %.0f samples; %d ground pixels wrong against the reference, %d apart from Full\n",
                LABELS[index], (index == 0) ? 480u : objects.low.width, (index == 0) ? 270u : objects.low.height,
                (index == 0) ? objects.uploaded.samples : objects.uploaded_low.samples, wrong[index], apart);
    if (index == 0) {
      CHECK(!objects.low.depth.uav);
    } else {
      CHECK(objects.low.width == width && objects.low.height == height && objects.uploaded_low.width == static_cast<float>(width)
            && objects.uploaded_low.render_height == static_cast<float>(height) && objects.uploaded_low.samples == samples);
      CHECK(wrong[index] < wrong[0] * 5 / 4);
    }
    if (index == 2) CHECK(apart > 20);  // CONTROL
  }
  WriteStrip(folder / "resolution_box.pgm", 480, 270, plain);

  // Case 3's banner: Light Through Banners + Coloured Shadows.
  const std::array<Box, 1> banner_scene = {{{.low = {-1.f, -0.6f, 12.f}, .high = {1.f, 1.4f, 12.05f}, .kind = Kind::THIN, .albedo = {0.8f, 0.1f, 0.1f}}}};
  const Frame banner = MakeFrame(480, 270, banner_scene, {0.2f, -0.6f, -0.77f});
  std::vector<uint8_t> banner_shadow(480 * 270, 0);
  const Vector to_sun = {-banner.sun[0], -banner.sun[1], -banner.sun[2]};
  for (size_t pixel = 0; pixel < banner_shadow.size(); ++pixel) {
    float distance = 0.f;
    banner_shadow[pixel] = (banner.picture.hit[pixel] == -1 && Occluder(banner_scene, banner.picture.position[pixel], to_sun, 1e30f, &distance) >= 0) ? 1 : 0;
  }
  sss::Settings coloured = Defaults();
  coloured.thin_light = 0.35f;
  coloured.coloured = true;
  std::array<std::vector<uint8_t>, 4> banners;
  double full_occlusion = 0.0;
  for (size_t index = 0; index < RESOLUTIONS.size(); ++index) {
    banners[index] = ReadView<uint8_t>(RunFrame(banner, at(coloured, RESOLUTIONS[index]), true));
    const auto tint = ReadView<uint32_t>(objects.tint_srv.Get());
    double occlusion = 0.0;
    double green = 0.0;
    uint32_t red = 0;
    int shadowed = 0;
    for (size_t pixel = 0; pixel < tint.size(); ++pixel) {
      red = std::max(red, tint[pixel] & 0xFFu);
      if (banner_shadow[pixel] == 0) continue;
      occlusion += banners[index][pixel] / 255.0;
      green += ((tint[pixel] >> 8) & 0xFFu) / 255.0;
      ++shadowed;
    }
    REQUIRE(shadowed > 100);
    occlusion /= shadowed;
    green /= shadowed;
    if (index == 0) full_occlusion = occlusion;
    std::printf("   the banner, %s: occlusion %.3f in its shadow (Full's x %.3f), filter green %.3f there, red at most %.3f\n", LABELS[index], occlusion,
                occlusion / full_occlusion, green, red / 255.0);
    CHECK(red < 26 && green > 0.5 && std::fabs((occlusion / full_occlusion) - 1.0) < 0.25);
  }
  WriteStrip(folder / "resolution_banner.pgm", 480, 270, banners);

  // Case 4's pole: Softer Away from the Caster.
  const std::array<Box, 1> pole_scene = {{{.low = {-0.1f, -2.f, 14.9f}, .high = {0.1f, 2.f, 15.1f}}}};
  const Frame pole = MakeFrame(960, 540, pole_scene, {0.8f, -0.6f, 0.f});
  sss::Settings softer = Defaults();
  softer.sun_disk = 2.f;
  std::array<std::vector<uint8_t>, 4> soft;
  std::vector<uint16_t> full_penumbra;
  for (size_t index = 0; index < RESOLUTIONS.size(); ++index) {
    soft[index] = ReadView<uint8_t>(RunFrame(pole, at(softer, RESOLUTIONS[index])));
    const auto penumbra = ReadView<uint16_t>(objects.penumbra_srv.Get());
    if (index == 0) {
      full_penumbra = penumbra;
      continue;
    }
    std::vector<float> ratios;
    for (size_t pixel = 0; pixel < penumbra.size(); ++pixel) {
      const float full_radius = HalfToFloat(full_penumbra[pixel]);
      const float radius = HalfToFloat(penumbra[pixel]);
      if (full_radius > 0.f && radius > 0.f) ratios.push_back(radius / full_radius);
    }
    REQUIRE(ratios.size() > 50);
    std::ranges::nth_element(ratios, ratios.begin() + static_cast<ptrdiff_t>(ratios.size() / 2));
    const float median = ratios[ratios.size() / 2];
    std::printf("   the pole, Softer 2, %s: penumbra over Full's median %.3f (%zu pixels)\n", LABELS[index], median, ratios.size());
    CHECK(median > 0.7f && median < 1.3f);
  }
  WriteStrip(folder / "resolution_pole.pgm", 960, 540, soft);

  // A torch beside the box at 1/2: its march at Full.
  const LightInstance torch = {.colour = {4.f, 2.f, 0.5f, 0.f}, .light = {2.f, -1.2f, 10.f, 6.f}};
  std::array<std::vector<uint32_t>, 2> masks;
  for (size_t index = 0; index < 2; ++index) {
    REQUIRE(LightFrames(box, std::span(&torch, 1), 4, 0.f, 0, at(Defaults(), RESOLUTIONS[index * 2])));
    masks[index] = ReadView<uint32_t>(objects.lights.mask_uav.Get());
  }
  size_t lights_apart = 0;
  for (size_t pixel = 0; pixel < masks[0].size(); ++pixel) lights_apart += (masks[0][pixel] != masks[1][pixel]) ? 1 : 0;
  std::printf("   Torch and Fire Shadows 4 at 1/2: %zu of %zu pixels' bits apart from Full's\n", lights_apart, masks[0].size());
  CHECK(lights_apart * 500 < masks[0].size());

  // Debug View 2 (its Screen Edge reads the light's point in render pixels) at 1/2: the dispatch list Full's.
  sss::Settings debug = Defaults();
  debug.debug_view = 2;
  RunFrame(box, debug);
  const auto full_parameters = ReadBuffer<uint8_t>(objects.parameters.Get());
  RunFrame(box, at(debug, 0.5f));
  const auto debug_parameters = ReadBuffer<uint8_t>(objects.parameters.Get());
  RunFrame(box, at(Defaults(), 0.5f));
  const auto low_parameters = ReadBuffer<uint8_t>(objects.parameters.Get());
  std::printf("   Debug View 2 at 1/2: the dispatch list %s Full's (CONTROL without it: %s)\n", (debug_parameters == full_parameters) ? "the same as" : "NOT",
              (low_parameters == full_parameters) ? "THE SAME" : "another");
  CHECK(debug_parameters == full_parameters && low_parameters != full_parameters);

  // Full again (sss::Update): the low textures let go.
  mock::Device mock_device;
  mock_device.native = device;
  mock_device.back_buffer.type = reshade::api::resource_type::texture_2d;
  mock_device.back_buffer.texture.width = 480;
  mock_device.back_buffer.texture.height = 270;
  mock::Swapchain swapchain;
  swapchain.owner = &mock_device;
  REQUIRE(objects.low.depth.uav && objects.low.occlusion.uav);
  sss::Update(&swapchain, at(Defaults(), 0.5f));
  CHECK(objects.low.depth.uav);
  sss::Update(&swapchain, Defaults());
  std::printf("   Full again: the low textures %s\n", (!objects.low.depth.uav && !objects.low.occlusion.uav && objects.low.width == 0) ? "let go" : "KEPT");
  CHECK(!objects.low.depth.uav && !objects.low.occlusion.uav && objects.low.width == 0);
}
#endif

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
    std::string name;
    sss::Settings settings;
    bool thin;
    size_t full;  // its row at Full Resolution
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
  std::vector<Row> rows = {{"rows off", base, false, 0},
                           {"Softness 2 px (today's blur)", softness, false, 1},
                           {"Auto Thickness", auto_thickness, false, 2},
                           {"Light Through Banners", banners, true, 3},
                           {"+ Coloured Shadows", coloured, true, 4},
                           {"Softer Away from the Caster 0.5", softer, false, 5}};
#ifndef SSS_ROW_BEFORE
  // Resolution (Experimental) below Full: rows off, + Coloured Shadows and Softer, each also against its row at Full.
  for (const auto& [label, resolution] : {std::pair{"3/4", 0.75f}, std::pair{"1/2", 0.5f}, std::pair{"1/4", 0.25f}}) {
    for (const size_t full : {0u, 4u, 5u}) {
      Row row = rows[full];
      row.name = "Resolution " + std::string(label) + ": " + rows[full].name;
      row.settings.resolution = resolution;
      rows.push_back(row);
    }
  }
#endif
  std::vector<double> times;
  for (const auto& row : rows) {
    times.push_back(GpuMilliseconds([&] { RunFrame(frame, row.settings, row.thin); }));
    std::printf("   %-52s %.3f ms (%+.3f)", row.name.c_str(), times.back(), times.back() - times[0]);
    if (row.full + 1 != times.size()) std::printf(", %+.3f against Full", times.back() - times[row.full]);
    std::printf("\n");
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

// --block-skip-gpu's times: db99b716b's Bend passes against this tree's on the hardware GPU at 3840 x 2160 (or `width` x
// `height`; WARP: --block-skip-time-warp, a smoke run whose times mean nothing), each the median of 30 frames of the
// module (the light mode's: its frame's first light draw, the pick and the march), in turns db99b716b's, this tree's,
// db99b716b's, this tree's: each row their means, the difference and the noise (A/A: db99b716b's two, B/B: this tree's),
// SLOWER where this tree's is slower by more than both. Rows: plain (Banners 0), the shipped default (sss_bend_extended:
// Light Through Banners 0.35 + Coloured Shadows, the banners' depth this frame), extended_find (Softer), Banners 0.35
// alone (the sun on the light mode's pass: sss_bend_extended without Find), the light mode with 8 and 32 torches
// (SkipTorches: 32 fill sss_lights' budget); Length 60, 120, 240, 480, 960 and 1920 (BlockSkipQuads' quads at 100%:
// (Length - 9) / 4; at 58%, Length x 0.58 rounded up to fours); 100% and 58% render scale; the box scene, the dense
// pillars (the skip's worst case) and the box with the sun in view (no light rows). Then Shogun 2's sss00 (the cascades)
// and sss01 (groupshared 15,872 -> 17,856 bytes: 6 -> 5 groups an SM on an RTX 5090) at its default 120 samples, 240,
// 480 and 3840 (its rows 0, 1, 2 and 5: 24, 54, 114 and 954 quads), the box scene.
static void TimeBlockSkip(const std::string& before_folder, D3D_DRIVER_TYPE driver, UINT width, UINT height) {
  std::printf("--block-skip-gpu: db99b716b's Bend passes against this tree's at %u x %u\n", width, height);
  MakeDevice(driver);
  const auto folder = std::filesystem::path(before_folder).parent_path() / "e2_cso";
  const BendPasses fresh = LoadBendPasses(folder, "new_");
  const BendPasses old = LoadBendPasses(folder, "old_");
  const std::vector<LightInstance> torches = SkipTorches();
  const auto buffer = MakeBuffer(static_cast<UINT>(torches.size() * sizeof(LightInstance)), D3D11_BIND_VERTEX_BUFFER, torches.data());
  const lights::Instances drawn = {.buffer = buffer.Get(), .first = 0, .count = static_cast<UINT>(torches.size()), .stride = sizeof(LightInstance),
                                   .position = offsetof(LightInstance, light), .colour = offsetof(LightInstance, colour)};
  const auto render_width = static_cast<UINT>(std::lround(width * 0.58));
  const auto render_height = static_cast<UINT>(std::lround(height * 0.58));
  auto& runtime = sss::internal::runtime;
  // `measure(fresh)`'s times in turns, said as a row.
  const auto time_row = [](const std::string& name, auto measure) {
    std::array<double, 4> milliseconds = {};
    for (size_t turn = 0; turn < milliseconds.size(); ++turn) milliseconds[turn] = measure((turn % 2) == 1);
    const double before = (milliseconds[0] + milliseconds[2]) / 2.0;
    const double after = (milliseconds[1] + milliseconds[3]) / 2.0;
    const double noise_before = std::abs(milliseconds[2] - milliseconds[0]);
    const double noise_after = std::abs(milliseconds[3] - milliseconds[1]);
    std::printf("   %-78s db99b716b %.3f ms, this tree %.3f ms (%+.3f; A/A %.3f, B/B %.3f)%s\n", name.c_str(), before, after, after - before, noise_before,
                noise_after, (after - before > std::max(noise_before, noise_after)) ? " SLOWER" : "");
  };
  struct Row {
    const char* name;
    float thin_light;
    bool coloured;
    float sun_disk;
    uint32_t lights;
  };
  constexpr std::array<Row, 6> ROWS = {{{"plain (Banners 0)", 0.f, false, 0.f, 0},
                                        {"extended (Banners 0.35 + Coloured: the default)", 0.35f, true, 0.f, 0},
                                        {"extended_find (Softer)", 0.f, false, 0.5f, 0},
                                        {"extended (Banners 0.35 alone: the light mode's pass)", 0.35f, false, 0.f, 0},
                                        {"light mode, 8 torches", 0.f, false, 0.f, 8},
                                        {"light mode, 32 torches", 0.f, false, 0.f, 32}}};
  struct Scene {
    const char* name;
    std::vector<Box> boxes;
    Vector sun;
    bool lights;  // the light mode's rows too
  };
  const std::array<Scene, 3> scenes = {{{"box", SkipBoxScene(), BOX_SUN, true},
                                        {"dense", SkipDenseScene(), BOX_SUN, true},
                                        {"box, sun in view", SkipBoxScene(), SKIP_SUN_IN_VIEW, false}}};
  for (const auto& scene : scenes) {
    for (const bool full : {true, false}) {
      const Frame frame = full ? MakeFrame(width, height, scene.boxes, scene.sun)
                               : ScaledFrame(width, height, render_width, render_height, scene.boxes, scene.sun);
      for (const float samples : {60.f, 120.f, 240.f, 480.f, 960.f, 1920.f}) {
        for (const auto& row : ROWS) {
          if (row.lights != 0 && !scene.lights) continue;
          sss::Settings settings = Defaults();
          settings.samples = samples;
          settings.thin_light = row.thin_light;
          settings.coloured = row.coloured;
          settings.sun_disk = row.sun_disk;
          const bool thin = row.thin_light > 0.f;
          char name[160];
          std::snprintf(name, sizeof(name), "%s %s, Length %4.0f, %s", scene.name, full ? "100%" : " 58%", samples, row.name);
          time_row(name, [&](bool fresh_side) {
            UseBendPasses(fresh_side ? fresh : old);
            twattila::frame_history::render_width = full ? 0 : render_width;
            twattila::frame_history::render_height = full ? 0 : render_height;
            if (row.lights == 0) return GpuMilliseconds([&] { RunFrame(frame, settings, thin); });
            REQUIRE(LightFrames(frame, std::span(torches), row.lights, 0.f, 0, settings));
            return GpuMilliseconds([&] {
              ++runtime.frame;
              runtime.computed_frame = runtime.frame;
              lights::OnLightDraw(context.Get(), drawn);
            });
          });
        }
      }
    }
  }
  twattila::frame_history::render_width = 0;
  twattila::frame_history::render_height = 0;

  const Frame frame = MakeFrame(width, height, SkipBoxScene(), BOX_SUN);
  const ShogunInputs inputs = MakeShogunInputs(frame, false);
  const Texture output = MakeTexture(width, height, DXGI_FORMAT_R8_UNORM, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS);
  ComPtr<ID3D11UnorderedAccessView> output_uav;
  REQUIRE(SUCCEEDED(device->CreateUnorderedAccessView(output.texture.Get(), nullptr, output_uav.GetAddressOf())));
  for (const std::string pass : {"s2_sss00", "s2_sss01"}) {
    const auto before = LoadCso((folder / ("old_" + pass + ".cso")).string());
    const auto after = LoadCso((folder / ("new_" + pass + ".cso")).string());
    for (const float samples_row : {0.f, 1.f, 2.f, 5.f}) {
      const auto settings = ShogunInjection({{offsetof(shogun2::ShaderInjectData, sss_samples), samples_row}});
      const int samples = 120 << static_cast<int>(samples_row);
      time_row("Shogun 2 " + pass + ", " + std::to_string(samples) + " samples" + ((samples_row == 0.f) ? " (the default)" : ""), [&](bool fresh_side) {
        return GpuMilliseconds([&] {
          RunShogunPass(frame, inputs, output, output_uav.Get(), fresh_side ? after.Get() : before.Get(), pass == "s2_sss00", true, settings.Get(), {}, false);
        });
      });
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
#ifdef SSS_ROW_BEFORE
  // run.ps1's build on 352a9b785's header: case 12's Full outputs alone, next to the shaders.
  if (mode != "--resolution-dump") return 2;
  std::ofstream file(std::string(argv[1]) + "\\resolution_full.bin", std::ios::binary);
  for (const auto& output : FullOutputs()) {
    const uint64_t size = output.size();
    file.write(reinterpret_cast<const char*>(&size), sizeof(size));
    file.write(reinterpret_cast<const char*>(output.data()), static_cast<std::streamsize>(output.size()));
  }
  std::printf("case 12's Full outputs on 352a9b785's header: %s\n", file.good() ? "written" : "NOT WRITTEN");
  return file.good() ? 0 : 1;
#else
  if (mode == "--block-skip-gpu") {
    TestBlockSkip(argv[1], D3D_DRIVER_TYPE_HARDWARE);
    TestBlockSkipShogun2(argv[1], D3D_DRIVER_TYPE_HARDWARE);
    TimeBlockSkip(argv[1], D3D_DRIVER_TYPE_HARDWARE, 3840, 2160);
    std::printf(failures == 0 ? "PASS\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
  }
  if (mode == "--block-skip-time-warp") {
    TimeBlockSkip(argv[1], D3D_DRIVER_TYPE_WARP, 480, 270);
    std::printf(failures == 0 ? "PASS\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
  }
  if (mode == "--block-skip") {
    TestBlockSkip(argv[1], D3D_DRIVER_TYPE_WARP);
    TestBlockSkipShogun2(argv[1], D3D_DRIVER_TYPE_WARP);
    std::printf(failures == 0 ? "PASS\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
  }
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
    TestBlockSkip(argv[1], D3D_DRIVER_TYPE_WARP);
    TestBlockSkipShogun2(argv[1], D3D_DRIVER_TYPE_WARP);
    TestResolution(argv[1]);
  }
  if (mode == "--time" || mode == "--time-only") Time(argv[1]);
  std::printf(failures == 0 ? "PASS\n" : "%d FAILED\n", failures);
  return failures == 0 ? 0 : 1;
#endif
}
