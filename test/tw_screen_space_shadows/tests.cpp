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
// --time: on the hardware GPU at 3840 x 2160, each row's GPU time (median of 30 frames), torches near and far, and the CPU
// time of a later light draw. --time-only: that alone.
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
// than 20 pixels apart).
constexpr int RUNS = 8;
static void CompareRuns(const char* what, const std::vector<std::vector<uint8_t>>& ours, const std::vector<std::vector<uint8_t>>& theirs,
                        bool same = true) {
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
    CHECK(over_half > 100 && varying < static_cast<int>(ours[0].size() / 500));
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

// Two frames of light draws (`instances` each), the second marching the first's: true if the second bound the lit bits.
template <typename Instance>
static bool LightFrames(const Frame& frame, std::span<const Instance> instances, uint32_t wanted, float range = 0.f) {
  const auto buffer = MakeBuffer(static_cast<UINT>(instances.size_bytes()), D3D11_BIND_VERTEX_BUFFER, instances.data());
  sss::Settings settings = Defaults();
  settings.lights = wanted;
  settings.light_range = range;
  bool bound = false;
  for (int repeat = 0; repeat < 2; ++repeat) {
    RunFrame(frame, settings);
    bound = lights::OnLightDraw(context.Get(), {.buffer = buffer.Get(),
                                                .first = 0,
                                                .count = static_cast<UINT>(instances.size()),
                                                .stride = sizeof(Instance),
                                                .position = offsetof(Instance, light),
                                                .colour = offsetof(Instance, colour)});
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

static void TestShogun2(const std::string& before_folder) {
  std::printf("8. Shogun 2's sss00-02 at the rows' defaults\n");
  MakeDevice(D3D_DRIVER_TYPE_WARP);
  const Frame frame = MakeFrame(480, 270, BOX_SCENE, BOX_SUN);
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
  const auto camera_buffer = MakeBuffer(sizeof(camera), D3D11_BIND_CONSTANT_BUFFER, camera.data());
  const auto shadow_map_buffer = MakeBuffer(sizeof(shadow_map), D3D11_BIND_CONSTANT_BUFFER, shadow_map.data());
  // The cascades' depths: random around the ground's and the box's.
  std::mt19937 random(8);
  std::uniform_real_distribution<float> map_depth(0.47f, 0.53f);
  std::vector<float> map_texels(MAP_SIZE * MAP_SIZE * 4);
  for (auto& texel : map_texels) texel = map_depth(random);
  const D3D11_TEXTURE2D_DESC map_desc = {MAP_SIZE, MAP_SIZE, 1, 4, DXGI_FORMAT_R32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
  std::array<D3D11_SUBRESOURCE_DATA, 4> map_slices = {};
  for (UINT slice = 0; slice < 4; ++slice) map_slices[slice] = {map_texels.data() + (slice * MAP_SIZE * MAP_SIZE), MAP_SIZE * 4, 0};
  ComPtr<ID3D11Texture2D> map;
  ComPtr<ID3D11ShaderResourceView> map_srv;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&map_desc, map_slices.data(), map.GetAddressOf())));
  REQUIRE(SUCCEEDED(device->CreateShaderResourceView(map.Get(), nullptr, map_srv.GetAddressOf())));
  const D3D11_SAMPLER_DESC comparison_desc = {D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP,
                                        D3D11_TEXTURE_ADDRESS_CLAMP, 0.f, 1, D3D11_COMPARISON_LESS_EQUAL, {}, 0.f, D3D11_FLOAT32_MAX};
  const D3D11_SAMPLER_DESC border_desc = {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_BORDER, D3D11_TEXTURE_ADDRESS_BORDER,
                                         D3D11_TEXTURE_ADDRESS_BORDER, 0.f, 1, D3D11_COMPARISON_NEVER, {1.f, 1.f, 1.f, 1.f}, 0.f, D3D11_FLOAT32_MAX};
  ComPtr<ID3D11SamplerState> comparison;
  ComPtr<ID3D11SamplerState> border;
  REQUIRE(SUCCEEDED(device->CreateSamplerState(&comparison_desc, comparison.GetAddressOf())));
  REQUIRE(SUCCEEDED(device->CreateSamplerState(&border_desc, border.GetAddressOf())));
  const Texture output = MakeTexture(width, height, DXGI_FORMAT_R8_UNORM, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS);
  ComPtr<ID3D11UnorderedAccessView> output_uav;
  REQUIRE(SUCCEEDED(device->CreateUnorderedAccessView(output.texture.Get(), nullptr, output_uav.GetAddressOf())));
  // Bend's sss constants for each listed dispatch: the game's thickness, edge threshold and contrast, the light coordinate and
  // the dispatch's offset.
  std::vector<ComPtr<ID3D11Buffer>> bend_buffers;
  std::vector<std::array<uint32_t, 3>> bend_groups;
  for (size_t dispatch = 0; dispatch < 8; ++dispatch) {
    if (arguments[dispatch][0] == 0 || arguments[dispatch][1] == 0 || arguments[dispatch][2] == 0) continue;
    std::array<uint32_t, 12> bend = {std::bit_cast<uint32_t>(0.005f), std::bit_cast<uint32_t>(0.02f), std::bit_cast<uint32_t>(4.f), 0u};
    for (int axis = 0; axis < 4; ++axis) bend[4 + axis] = static_cast<uint32_t>(parameters[0][axis]);
    bend[8] = static_cast<uint32_t>(parameters[1 + dispatch][0]);
    bend[9] = static_cast<uint32_t>(parameters[1 + dispatch][1]);
    bend_buffers.push_back(MakeBuffer(sizeof(bend), D3D11_BIND_CONSTANT_BUFFER, bend.data()));
    bend_groups.push_back(arguments[dispatch]);
  }
  REQUIRE(!bend_buffers.empty());
  // shader_injection: all 0 (the rows' defaults), or one row at a value.
  const auto injection = [](size_t offset, float value) {
    std::vector<float> floats((sizeof(shogun2::ShaderInjectData) + 15) / 16 * 4, 0.f);
    floats[offset / sizeof(float)] = value;
    return MakeBuffer(static_cast<UINT>(floats.size() * sizeof(float)), D3D11_BIND_CONSTANT_BUFFER, floats.data());
  };
  const auto defaults = injection(0, 0.f);
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
    const auto load = [&pass](const std::string& folder) {
      const auto code = ReadFile(folder + "\\" + pass.name + ".cso");
      REQUIRE(!code.empty());
      ComPtr<ID3D11ComputeShader> shader;
      REQUIRE(SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, shader.GetAddressOf())));
      return shader;
    };
    const auto before = load(before_folder);
    const auto after = load(after_folder.string());
    const auto control = injection(pass.control_row, 2.f);
    const auto run = [&](ID3D11ComputeShader* shader, ID3D11Buffer* settings) {
      constexpr std::array<UINT, 4> CLEAR = {};
      context->ClearUnorderedAccessViewUint(output_uav.Get(), CLEAR.data());
      context->CSSetShader(shader, nullptr, 0);
      context->CSSetConstantBuffers(13, 1, &settings);
      context->CSSetConstantBuffers(0, 1, camera_buffer.GetAddressOf());
      context->CSSetUnorderedAccessViews(0, 1, output_uav.GetAddressOf(), nullptr);
      if (pass.cascades) {
        context->CSSetConstantBuffers(1, 1, shadow_map_buffer.GetAddressOf());
        const std::array<ID3D11ShaderResourceView*, 4> views = {map_srv.Get(), frame.colour.srv.Get(), frame.depth.srv.Get(), frame.depth.srv.Get()};
        context->CSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
        const std::array<ID3D11SamplerState*, 2> samplers = {comparison.Get(), border.Get()};
        context->CSSetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());
      } else {
        context->CSSetShaderResources(0, 1, frame.depth.srv.GetAddressOf());
        context->CSSetSamplers(0, 1, border.GetAddressOf());
      }
      if (pass.bend) {
        for (size_t dispatch = 0; dispatch < bend_buffers.size(); ++dispatch) {
          context->CSSetConstantBuffers(pass.cascades ? 2 : 1, 1, bend_buffers[dispatch].GetAddressOf());
          context->Dispatch(bend_groups[dispatch][0], bend_groups[dispatch][1], bend_groups[dispatch][2]);
        }
      } else {
        context->Dispatch((width + 31) / 32, (height + 31) / 32, 1);
      }
      context->ClearState();
      return ReadTexture<uint8_t>(output.texture.Get());
    };
    std::vector<std::vector<uint8_t>> ours;
    std::vector<std::vector<uint8_t>> theirs;
    std::vector<std::vector<uint8_t>> controls;
    for (int round = 0; round < RUNS; ++round) {
      ours.push_back(run(after.Get(), defaults.Get()));
      theirs.push_back(run(before.Get(), defaults.Get()));
      controls.push_back(run(after.Get(), control.Get()));
    }
    CompareRuns((std::string(pass.name) + " at the rows' defaults against fc24dbce's").c_str(), ours, theirs);
    CompareRuns((std::string("CONTROL ") + pass.name + " with " + (pass.bend ? "Bend's contrast" : "shadow map stability") + " at 2").c_str(),
                controls, theirs, false);
  }
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

static void Time() {
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
    TestReplay();
    TestRowsTurnedOff();
    TestShogun2(argv[1]);
  }
  if (mode == "--time" || mode == "--time-only") Time();
  std::printf(failures == 0 ? "PASS\n" : "%d FAILED\n", failures);
  return failures == 0 ? 0 : 1;
}
