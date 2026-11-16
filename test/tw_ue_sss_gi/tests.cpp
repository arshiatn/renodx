// Bend's screen-space shadows and GT-VBGI on the Unreal games (talesofarise_dlss/ue_screen_space_shadows.hpp,
// ue_light_bounce.hpp) on WARP, through their Compute and Apply with a game's embedded shaders (run.ps1 builds this
// twice): build\expeditionsrome.include with -DUE_GAME_ROME (UE 4.23: View rows 8+ moved by 4, the sun from the
// light's FDeferredLightUniformStruct, GT-VBGI added into a target x the diffuse colour) or build\talesofarise.include
// (UE 4.21: the sun from Bandai's ScreenToShadow, GT-VBGI as the light arriving for its lighting pass).
// tests.exe [Tales: 0x95802ADA.ps_5_0.cso, the game's contact shadows as dumped]
// The scenes, ray-traced on the CPU into Unreal's depth (device Z, reversed infinite, near 10 cm), G-buffer and View
// (centimetres): a camera looking 25 degrees down at a ground plane 2 m below it (60 degrees high).
// 1. Bend: a 1.5 m box with the sun beyond it: of the ground pixels a CPU ray toward the sun finds shadowed (within 60
//    pixels of the box), at least 85% occluded (> 0.5); of those it finds lit, at most 2%. (Bend takes each pixel as
//    thin: a sun from the side, its rays entering the box through a face the camera does not see, leaves most of that
//    shadow out, as on the Total War games.) The sun without shadows (Rome: ShadowedBits 0; Tales: no ScreenToShadow)
//    occludes nothing; off: no pass (null). UE_SSS_MAP=1 prints the box's surroundings (# box, S / s shadowed found /
//    missed, x lit but occluded).
// 2. GT-VBGI: a red-lit wall beside black ground, 16 frames: the ground near the wall gets red light (its mean over 0.05,
//    10 x its green), more than ground farther (the search reaches a quarter of the view's height); the sky none;
//    Strength 0: no pass, the target untouched. Rome: metallic ground (no diffuse colour) none. Tales: the light arriving
//    (Rome's x the ground's diffuse colour 0.5), the same on metallic ground. Then the rays' trace in place of its own
//    (SetRayTracedTrace, Tales' Ray Tracing in the Helper): a stub's green light replaces the wall's red; a stub tracing
//    nothing gives GT-VBGI's own trace back (CONTROL: the red returns).
// 3. Tales: contactshadow00 (Bandai's character contact shadows, both marches) with SI.screen_space_shadows 0 as the
//    game's own (the dumped cso) within 1e-5 on the box scene with character pixels; CONTROL: with it 1 (Bend's in place
//    of its march toward the light) it must differ.
#include <windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <DirectXPackedVector.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>
#include "src/games/talesofarise_dlss/ue_light_bounce.hpp"
#include "src/games/talesofarise_dlss/ue_screen_space_shadows.hpp"
#ifndef UE_GAME_ROME
#include "src/games/talesofarise/shared.h"
#endif

using Microsoft::WRL::ComPtr;
using Vector = std::array<float, 3>;
using Row = std::array<float, 4>;

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (false)
#define REQUIRE(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

extern "C" __declspec(dllexport) const char* const NAME = "tw_ue_sss_gi";
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}

#ifdef UE_GAME_ROME
constexpr uint32_t SHIFT = 4;
#else
constexpr uint32_t SHIFT = 0;
#endif

constexpr UINT WIDTH = 384;
constexpr UINT HEIGHT = 216;
constexpr float NEAR_CM = 10.f;
constexpr float NEAR_M = 0.1f;
constexpr float GROUND_Z = -2.f;
constexpr float TAN_HALF_Y = 0.57735027f;
constexpr float P11 = 1.f / TAN_HALF_Y;
constexpr float P00 = P11 * static_cast<float>(HEIGHT) / static_cast<float>(WIDTH);

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;

static float Dot(const Vector& a, const Vector& b) { return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]); }
static Vector Scale(const Vector& v, float s) { return {v[0] * s, v[1] * s, v[2] * s}; }
static Vector Add(const Vector& a, const Vector& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
static Vector Cross(const Vector& a, const Vector& b) {
  return {(a[1] * b[2]) - (a[2] * b[1]), (a[2] * b[0]) - (a[0] * b[2]), (a[0] * b[1]) - (a[1] * b[0])};
}
static Vector Normalize(const Vector& v) { return Scale(v, 1.f / std::sqrt(Dot(v, v))); }

// World: x right, y forward, z up; the camera at the origin (the translated world), pitched 25 degrees down.
static const Vector RIGHT = {1.f, 0.f, 0.f};
static const Vector UP = {0.f, std::sin(0.43633231f), std::cos(0.43633231f)};
static const Vector FORWARD = {0.f, std::cos(0.43633231f), -std::sin(0.43633231f)};

// A pixel's ray: its view depth is the distance along it (FORWARD's share 1).
static Vector PixelRay(UINT x, UINT y) {
  const float ndc_x = ((static_cast<float>(x) + 0.5f) / WIDTH * 2.f) - 1.f;
  const float ndc_y = 1.f - ((static_cast<float>(y) + 0.5f) / HEIGHT * 2.f);
  return Add(Add(Scale(RIGHT, ndc_x / P00), Scale(UP, ndc_y / P11)), FORWARD);
}

struct Box {
  Vector low;
  Vector high;
};
static const Box BOX = {{-0.5f, 7.5f, GROUND_Z}, {0.5f, 8.5f, GROUND_Z + 1.5f}};
// The GI scene's wall: x = -1.5 m facing +x.
constexpr float WALL_X = -1.5f;

static std::optional<float> HitBox(const Vector& origin, const Vector& direction, const Box& box) {
  float near_t = 0.f;
  float far_t = 1e30f;
  for (int axis = 0; axis < 3; ++axis) {
    if (std::abs(direction[axis]) < 1e-12f) {
      if (origin[axis] < box.low[axis] || origin[axis] > box.high[axis]) return std::nullopt;
      continue;
    }
    float t0 = (box.low[axis] - origin[axis]) / direction[axis];
    float t1 = (box.high[axis] - origin[axis]) / direction[axis];
    if (t0 > t1) std::swap(t0, t1);
    near_t = std::max(near_t, t0);
    far_t = std::min(far_t, t1);
  }
  if (near_t > far_t || far_t <= 1e-5f) return std::nullopt;
  return near_t;
}

enum Surface : int { SKY, GROUND, BOX_FACE, WALL };
struct Pixel {
  Surface surface = SKY;
  float depth = 0.f;  // view depth, metres
  Vector position = {};
  Vector normal = {};
};

// The scene seen through each pixel: the ground, and the box or the wall.
static std::vector<Pixel> Trace(bool wall) {
  std::vector<Pixel> pixels(WIDTH * HEIGHT);
  for (UINT y = 0; y < HEIGHT; ++y) {
    for (UINT x = 0; x < WIDTH; ++x) {
      const Vector ray = PixelRay(x, y);
      Pixel& pixel = pixels[(y * WIDTH) + x];
      float best = 1e30f;
      if (ray[2] < 0.f) {
        best = GROUND_Z / ray[2];
        pixel = {.surface = GROUND, .depth = best, .position = Scale(ray, best), .normal = {0.f, 0.f, 1.f}};
      }
      if (!wall) {
        if (const auto t = HitBox({0.f, 0.f, 0.f}, ray, BOX); t && *t < best) {
          const Vector position = Scale(ray, *t);
          Vector normal = {};
          for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(position[axis] - BOX.low[axis]) < 1e-3f) normal[axis] = -1.f;
            if (std::abs(position[axis] - BOX.high[axis]) < 1e-3f) normal[axis] = 1.f;
          }
          best = *t;
          pixel = {.surface = BOX_FACE, .depth = best, .position = position, .normal = normal};
        }
      } else if (ray[0] < 0.f) {
        const float t = WALL_X / ray[0];
        const Vector position = Scale(ray, t);
        if (t < best && position[1] > 1.f && position[1] < 40.f && position[2] > GROUND_Z && position[2] < 3.f) {
          pixel = {.surface = WALL, .depth = t, .position = position, .normal = {1.f, 0.f, 0.f}};
        }
      }
    }
  }
  return pixels;
}

// UE's View (4.21 rows; 4.23: rows from 8 on 4 later), centimetres, the camera still since the last frame.
static std::vector<Row> ViewRows() {
  std::vector<Row> rows(140 + SHIFT, Row{});
  const auto row = [&](uint32_t index) -> Row& { return rows[index + ((index >= 8) ? SHIFT : 0)]; };
  for (int axis = 0; axis < 3; ++axis) {
    row(8 + axis) = {RIGHT[axis], UP[axis], FORWARD[axis], 0.f};                  // TranslatedWorldToView
    row(0 + axis) = {RIGHT[axis] * P00, UP[axis] * P11, 0.f, FORWARD[axis]};     // TranslatedWorldToClip
    row(83 + axis) = row(0 + axis);                                               // PrevTranslatedWorldToClip
  }
  row(11) = {0.f, 0.f, 0.f, 1.f};
  row(3) = {0.f, 0.f, NEAR_CM, 0.f};
  row(86) = row(3);
  row(12) = {RIGHT[0], RIGHT[1], RIGHT[2], 0.f};  // ViewToTranslatedWorld
  row(13) = {UP[0], UP[1], UP[2], 0.f};
  row(14) = {FORWARD[0], FORWARD[1], FORWARD[2], 0.f};
  row(15) = {0.f, 0.f, 0.f, 1.f};
  row(24) = {P00, 0.f, 0.f, 0.f};  // ViewToClip, reversed infinite
  row(25) = {0.f, P11, 0.f, 0.f};
  row(26) = {0.f, 0.f, 0.f, 1.f};
  row(27) = {0.f, 0.f, NEAR_CM, 0.f};
  // ScreenToWorld (rows 0-2: screen x depth, y x depth, depth).
  row(44) = {RIGHT[0] / P00, RIGHT[1] / P00, RIGHT[2] / P00, 0.f};
  row(45) = {UP[0] / P11, UP[1] / P11, UP[2] / P11, 0.f};
  row(46) = {FORWARD[0], FORWARD[1], FORWARD[2], 0.f};
  row(57) = {0.f, 0.f, 1.f / NEAR_CM, 0.f};  // InvDeviceZToWorldZTransform
  row(58) = {0.5f, -0.5f, 0.5f, 0.5f};      // ScreenPositionScaleBias
  row(122) = {static_cast<float>(WIDTH), static_cast<float>(HEIGHT), 1.f / WIDTH, 1.f / HEIGHT};
  row(123) = row(122);
  return rows;
}

static ComPtr<ID3D11Buffer> ConstantBuffer(const void* data, UINT bytes) {
  const D3D11_BUFFER_DESC desc = {.ByteWidth = (bytes + 15u) & ~15u, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  std::vector<uint8_t> padded(desc.ByteWidth, 0);
  std::memcpy(padded.data(), data, bytes);
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = padded.data()};
  ComPtr<ID3D11Buffer> buffer;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&desc, &initial, buffer.GetAddressOf())));
  return buffer;
}

struct Texture {
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> srv;
  ComPtr<ID3D11RenderTargetView> rtv;
};

static Texture MakeTexture(DXGI_FORMAT format, const void* data, UINT pitch, bool target = false) {
  const D3D11_TEXTURE2D_DESC desc = {
      .Width = WIDTH,
      .Height = HEIGHT,
      .MipLevels = 1,
      .ArraySize = 1,
      .Format = format,
      .SampleDesc = {.Count = 1},
      .Usage = D3D11_USAGE_DEFAULT,
      .BindFlags = static_cast<UINT>(D3D11_BIND_SHADER_RESOURCE | (target ? D3D11_BIND_RENDER_TARGET : 0)),
  };
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = data, .SysMemPitch = pitch};
  Texture texture;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, (data != nullptr) ? &initial : nullptr, texture.texture.GetAddressOf())));
  REQUIRE(SUCCEEDED(device->CreateShaderResourceView(texture.texture.Get(), nullptr, texture.srv.GetAddressOf())));
  if (target) REQUIRE(SUCCEEDED(device->CreateRenderTargetView(texture.texture.Get(), nullptr, texture.rtv.GetAddressOf())));
  return texture;
}

// A texture's top-left WIDTH x HEIGHT as floats, `channels` a texel (R8_UNORM, R16G16B16A16_FLOAT, R32G32B32A32_FLOAT).
static std::vector<float> Read(ID3D11Resource* resource, size_t channels) {
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
  std::vector<float> values(static_cast<size_t>(WIDTH) * HEIGHT * channels);
  for (UINT y = 0; y < HEIGHT; ++y) {
    const auto* line = static_cast<const uint8_t*>(mapped.pData) + (static_cast<size_t>(y) * mapped.RowPitch);
    for (UINT x = 0; x < WIDTH; ++x) {
      for (size_t c = 0; c < channels; ++c) {
        float& value = values[(((static_cast<size_t>(y) * WIDTH) + x) * channels) + c];
        if (desc.Format == DXGI_FORMAT_R8_UNORM) {
          value = static_cast<float>(line[x]) / 255.f;
        } else if (desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
          value = DirectX::PackedVector::XMConvertHalfToFloat(reinterpret_cast<const uint16_t*>(line)[(x * 4) + c]);
        } else {
          value = reinterpret_cast<const float*>(line)[(x * 4) + c];
        }
      }
    }
  }
  context->Unmap(staging.Get(), 0);
  return values;
}

static Texture DepthTexture(const std::vector<Pixel>& pixels) {
  std::vector<float> depth(pixels.size());
  for (size_t index = 0; index < pixels.size(); ++index) {
    depth[index] = (pixels[index].surface == SKY) ? 0.f : NEAR_M / pixels[index].depth;
  }
  return MakeTexture(DXGI_FORMAT_R32_FLOAT, depth.data(), WIDTH * 4);
}

// ---- 1. Bend ---------------------------------------------------------------------------------------------------------

static const Vector TOWARD_SUN = Normalize({0.f, 0.55f, 0.6f});

// The sun's constants as the game binds them at the pass (Rome: the light's FDeferredLightUniformStruct, 160 bytes;
// Tales: Bandai's ScreenToShadow rows 28-30 of an orthographic shadow map along the sun).
static ComPtr<ID3D11Buffer> SunConstants(bool shadowed) {
#ifdef UE_GAME_ROME
  std::array<Row, 10> light = {};
  const uint32_t bits = shadowed ? 1u : 0u;
  std::memcpy(light[2].data(), &bits, 4);
  light[5] = {TOWARD_SUN[0], TOWARD_SUN[1], TOWARD_SUN[2], 1.f};
  return ConstantBuffer(light.data(), sizeof(light));
#else
  std::array<Row, 32> shadow = {};
  if (shadowed) {
    const Vector u = Normalize(Cross(TOWARD_SUN, {0.f, 0.f, 1.f}));
    const Vector v = Cross(TOWARD_SUN, u);
    const std::array<Vector, 3> axes = {u, v, Scale(TOWARD_SUN, -1.f)};  // shadow x, y, depth (away from the sun)
    const std::array<Vector, 3> screen_to_world = {Scale(RIGHT, 1.f / P00), Scale(UP, 1.f / P11), FORWARD};
    for (int k = 0; k < 3; ++k) {
      for (int c = 0; c < 3; ++c) shadow[28 + k][c] = Dot(screen_to_world[k], axes[c]) * 0.001f;
    }
  }
  return ConstantBuffer(shadow.data(), sizeof(shadow));
#endif
}

static void TestBend() {
  const auto pixels = Trace(false);
  const Texture depth = DepthTexture(pixels);
  const auto view_rows = ViewRows();
  const auto view = ConstantBuffer(view_rows.data(), static_cast<UINT>(view_rows.size() * sizeof(Row)));
  const twattila::screen_space_shadows::Settings settings = {.enabled = true};
  const auto run = [&](bool shadowed, const twattila::screen_space_shadows::Settings& with) -> std::vector<float> {
    const talesofarise::ue_screen_space_shadows::Inputs inputs = {
        .depth = depth.srv.Get(), .sun = {.buffer = SunConstants(shadowed)}, .view = {.buffer = view}, .render_share = 1.f};
    ID3D11ShaderResourceView* occlusion = talesofarise::ue_screen_space_shadows::Compute(context.Get(), inputs, with);
    if (occlusion == nullptr) return {};
    ComPtr<ID3D11Resource> resource;
    occlusion->GetResource(resource.GetAddressOf());
    return Read(resource.Get(), 1);
  };

  const auto occlusion = run(true, settings);
  REQUIRE(!occlusion.empty());
  // The box's pixels' bounds, 60 pixels around them.
  UINT left = WIDTH, right = 0, top = HEIGHT, bottom = 0;
  for (UINT y = 0; y < HEIGHT; ++y) {
    for (UINT x = 0; x < WIDTH; ++x) {
      if (pixels[(y * WIDTH) + x].surface != BOX_FACE) continue;
      left = std::min(left, x);
      right = std::max(right, x);
      top = std::min(top, y);
      bottom = std::max(bottom, y);
    }
  }
  REQUIRE(right > left);
  int shadowed = 0, found = 0, lit = 0, wrong = 0;
  for (UINT y = 0; y < HEIGHT; ++y) {
    for (UINT x = 0; x < WIDTH; ++x) {
      const Pixel& pixel = pixels[(y * WIDTH) + x];
      if (pixel.surface != GROUND || x + 60 < left || x > right + 60 || y + 60 < top || y > bottom + 60) continue;
      // Next to the box's outline: Bend's depth there is the box's half the time (a pixel either way).
      bool edge = false;
      for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
          const int nx = static_cast<int>(x) + dx;
          const int ny = static_cast<int>(y) + dy;
          if (nx >= 0 && ny >= 0 && nx < static_cast<int>(WIDTH) && ny < static_cast<int>(HEIGHT)) {
            edge |= pixels[(ny * WIDTH) + nx].surface == BOX_FACE;
          }
        }
      }
      if (edge) continue;
      const bool in_shadow = HitBox(Add(pixel.position, Scale(TOWARD_SUN, 1e-3f)), TOWARD_SUN, BOX).has_value();
      const bool occluded = occlusion[(y * WIDTH) + x] > 0.5f;
      if (in_shadow) {
        ++shadowed;
        found += occluded ? 1 : 0;
      } else {
        ++lit;
        wrong += occluded ? 1 : 0;
      }
    }
  }
  std::printf("Bend: %d of %d shadowed ground pixels occluded, %d of %d lit ones\n", found, shadowed, wrong, lit);
  if (std::getenv("UE_SSS_MAP") != nullptr) {
    for (UINT y = (top > 10 ? top - 10 : 0); y < std::min(HEIGHT, bottom + 30); ++y) {
      std::string line;
      for (UINT x = (left > 30 ? left - 30 : 0); x < std::min(WIDTH, right + 60); ++x) {
        const Pixel& pixel = pixels[(y * WIDTH) + x];
        const bool in_shadow = pixel.surface == GROUND && HitBox(Add(pixel.position, Scale(TOWARD_SUN, 1e-3f)), TOWARD_SUN, BOX).has_value();
        const bool occluded = occlusion[(y * WIDTH) + x] > 0.5f;
        line += (pixel.surface == BOX_FACE) ? '#' : (in_shadow ? (occluded ? 'S' : 's') : (occluded ? 'x' : '.'));
      }
      std::printf("%s\n", line.c_str());
    }
  }
  CHECK(shadowed > 200);
  CHECK(found >= shadowed * 85 / 100);
  CHECK(wrong <= lit * 2 / 100);

  const auto unshadowed = run(false, settings);
  CHECK(!unshadowed.empty() && std::ranges::all_of(unshadowed, [](float value) { return value == 0.f; }));
  CHECK(run(true, {}).empty());
}

// ---- 2. GT-VBGI ------------------------------------------------------------------------------------------------------

static void TestBounce() {
  const auto pixels = Trace(true);
  const Texture depth = DepthTexture(pixels);
  const auto view_rows = ViewRows();
  const auto view = ConstantBuffer(view_rows.data(), static_cast<UINT>(view_rows.size() * sizeof(Row)));
  std::vector<float> light(pixels.size() * 4, 0.f);
  std::vector<float> gbuffer_a(pixels.size() * 4, 0.f);
  std::vector<uint8_t> gbuffer_b(pixels.size() * 4, 0);
  std::vector<uint8_t> gbuffer_c(pixels.size() * 4, 0);
  for (size_t index = 0; index < pixels.size(); ++index) {
    const Pixel& pixel = pixels[index];
    if (pixel.surface == WALL) light[(index * 4) + 0] = 4.f;
    for (int c = 0; c < 3; ++c) gbuffer_a[(index * 4) + c] = (pixel.normal[c] * 0.5f) + 0.5f;
    // Metallic ground right of x = 1 m (no diffuse colour in Rome), the default lit model (1); the sky unlit (0).
    const bool metallic = pixel.surface == GROUND && pixel.position[0] > 1.f;
    gbuffer_b[(index * 4) + 0] = metallic ? 255 : 0;
    gbuffer_b[(index * 4) + 1] = 128;
    gbuffer_b[(index * 4) + 2] = 128;
    gbuffer_b[(index * 4) + 3] = (pixel.surface == SKY) ? 0 : 1;
    for (int c = 0; c < 3; ++c) gbuffer_c[(index * 4) + c] = (pixel.surface == SKY) ? 0 : 128;
  }
  const Texture light_texture = MakeTexture(DXGI_FORMAT_R32G32B32A32_FLOAT, light.data(), WIDTH * 16);
  const Texture a = MakeTexture(DXGI_FORMAT_R32G32B32A32_FLOAT, gbuffer_a.data(), WIDTH * 16);
  const Texture b = MakeTexture(DXGI_FORMAT_R8G8B8A8_UNORM, gbuffer_b.data(), WIDTH * 4);
  const Texture c = MakeTexture(DXGI_FORMAT_R8G8B8A8_UNORM, gbuffer_c.data(), WIDTH * 4);
  const Texture target = MakeTexture(DXGI_FORMAT_R16G16B16A16_FLOAT, nullptr, 0, true);
  const talesofarise::ue_light_bounce::Inputs inputs = {
      .light = light_texture.srv.Get(),
      .depth = depth.srv.Get(),
      .gbuffer_a = a.srv.Get(),
      .gbuffer_b = b.srv.Get(),
      .gbuffer_c = c.srv.Get(),
      .view = {.buffer = view},
      .render_size = {WIDTH, HEIGHT},
  };
  // Global Illumination's Faster (Recommended) at Strength 1, GT-VBAO's thickness 0.1 m.
  const twattila::light_bounce::Sliders sliders = {.strength = 1.f, .radius = 20.f, .thickness = 0.1f, .quality = 2.f, .directions = 0.f,
                                                    .resolution = 2.f, .temporal = 1.f, .checkerboard = 1.f, .auto_thickness = 1.f,
                                                    .denoise = 1.f};
  constexpr std::array<FLOAT, 4> ZERO = {};
  std::vector<float> bounce;
  for (uint64_t frame = 0; frame < 16; ++frame) {
    context->ClearRenderTargetView(target.rtv.Get(), ZERO.data());
#ifdef UE_GAME_ROME
    CHECK(talesofarise::ue_light_bounce::Apply(context.Get(), inputs, talesofarise::ue_light_bounce::ConstantsOf(sliders, frame), true,
                                               target.rtv.Get()) == nullptr);
#else
    ID3D11ShaderResourceView* irradiance =
        talesofarise::ue_light_bounce::Apply(context.Get(), inputs, talesofarise::ue_light_bounce::ConstantsOf(sliders, frame), true, nullptr);
    REQUIRE(irradiance != nullptr);
    if (frame == 15) {
      ComPtr<ID3D11Resource> resource;
      irradiance->GetResource(resource.GetAddressOf());
      bounce = Read(resource.Get(), 4);
    }
#endif
  }
#ifdef UE_GAME_ROME
  bounce = Read(target.texture.Get(), 4);
#endif
  // The ground's mean red and green: beside the wall (x -1.4 to -0.6 m), 3-4.5 m from it, its metallic part (x 1-2.5 m).
  const auto mean = [&](float low_x, float high_x, int channel) {
    double sum = 0.0;
    int count = 0;
    for (size_t index = 0; index < pixels.size(); ++index) {
      const Pixel& pixel = pixels[index];
      if (pixel.surface != GROUND || pixel.position[0] < low_x || pixel.position[0] > high_x || pixel.position[1] < 4.f
          || pixel.position[1] > 12.f) {
        continue;
      }
      sum += bounce[(index * 4) + channel];
      ++count;
    }
    return (count > 0) ? static_cast<float>(sum / count) : -1.f;
  };
  const float near_red = mean(-1.4f, -0.6f, 0);
  const float near_green = mean(-1.4f, -0.6f, 1);
  const float far_red = mean(1.5f - 0.0f, 3.f, 0);
  std::printf("GT-VBGI: ground beside the wall red %.4f green %.4f, 3-4.5 m away red %.4f\n", near_red, near_green, far_red);
  CHECK(near_red > 0.05f);
  CHECK(near_red > 10.f * near_green);
#ifdef UE_GAME_ROME
  // 1.5-3 m: the metallic ground, no diffuse colour.
  CHECK(far_red == 0.f);
  const float lit_far = mean(0.2f, 1.f, 0);
  std::printf("GT-VBGI: dielectric ground 1.7-2.5 m away red %.4f\n", lit_far);
  CHECK(near_red > lit_far && lit_far > 0.f);
#else
  CHECK(near_red > far_red && far_red > 0.f);
#endif
  float sky = 0.f;
  for (size_t index = 0; index < pixels.size(); ++index) {
    if (pixels[index].surface == SKY) sky = std::max({sky, bounce[index * 4], bounce[(index * 4) + 1], bounce[(index * 4) + 2]});
  }
  std::printf("GT-VBGI: the sky's most %.6f\n", sky);
  CHECK(sky == 0.f);

  // The rays' trace in place of GT-VBGI's (twattila::light_bounce::SetRayTracedTrace: Tales' Ray Tracing in the Helper):
  // a stub giving every texel green light 0.25 for 32 frames (the screen-space frames' history fading out): the ground
  // beside the wall gets the stub's green and loses the wall's red; then one tracing nothing (false) for 16 frames:
  // GT-VBGI's own trace again (the red back).
  // The stub keeps each texel's alpha (its surface's distance, as the helper's trace writes it) from the last screen-space
  // trace, in its own context state.
  static std::array<UINT, 2> traced_size = {};
  static bool trace_rays = true;
  twattila::light_bounce::SetRayTracedTrace([](ID3D11DeviceContext* native, ID3D11Texture2D* prepared, UINT width, UINT height, UINT,
                                               ID3D11Texture2D* traced) {
    static ComPtr<ID3DDeviceContextState> stub_state;
    static ComPtr<ID3D11VertexShader> full_screen;
    static ComPtr<ID3D11PixelShader> green;
    static ComPtr<ID3D11Texture2D> kept;
    static ComPtr<ID3D11ShaderResourceView> kept_view;
    ComPtr<ID3D11DeviceContext1> native1;
    if (!trace_rays || prepared == nullptr || FAILED(native->QueryInterface(IID_PPV_ARGS(native1.GetAddressOf())))) return false;
    if (!stub_state) {
      const std::string vs = "float4 main(uint id : SV_VertexID) : SV_Position { const float2 uv = float2((id << 1) & 2, id & 2);"
                             " return float4(uv * float2(2.f, -2.f) + float2(-1.f, 1.f), 0.f, 1.f); }";
      const std::string ps = "Texture2D<float4> kept : register(t0); float4 main(float4 p : SV_Position) : SV_Target"
                             " { return float4(0.f, 0.25f, 0.f, kept.Load(int3(p.xy, 0)).a); }";
      ComPtr<ID3DBlob> vs_code;
      ComPtr<ID3DBlob> ps_code;
      D3D11_TEXTURE2D_DESC desc = {};
      traced->GetDesc(&desc);
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      const auto feature_level = device->GetFeatureLevel();
      REQUIRE(SUCCEEDED(D3DCompile(vs.data(), vs.size(), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vs_code.GetAddressOf(), nullptr)));
      REQUIRE(SUCCEEDED(D3DCompile(ps.data(), ps.size(), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, ps_code.GetAddressOf(), nullptr)));
      REQUIRE(SUCCEEDED(device->CreateVertexShader(vs_code->GetBufferPointer(), vs_code->GetBufferSize(), nullptr, full_screen.GetAddressOf())));
      REQUIRE(SUCCEEDED(device->CreatePixelShader(ps_code->GetBufferPointer(), ps_code->GetBufferSize(), nullptr, green.GetAddressOf())));
      REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, kept.GetAddressOf())));
      REQUIRE(SUCCEEDED(device->CreateShaderResourceView(kept.Get(), nullptr, kept_view.GetAddressOf())));
      ComPtr<ID3D11Device1> device1;
      REQUIRE(SUCCEEDED(device.As(&device1)));
      REQUIRE(SUCCEEDED(device1->CreateDeviceContextState(0, &feature_level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1), nullptr,
                                                         stub_state.GetAddressOf())));
    }
    ComPtr<ID3D11RenderTargetView> target_view;
    REQUIRE(SUCCEEDED(device->CreateRenderTargetView(traced, nullptr, target_view.GetAddressOf())));
    native->CopyResource(kept.Get(), traced);
    const talesofarise::dlss::ContextStateScope scope(native1.Get(), stub_state.Get());
    D3D11_TEXTURE2D_DESC desc = {};
    traced->GetDesc(&desc);
    const D3D11_VIEWPORT viewport = {.Width = static_cast<FLOAT>(desc.Width), .Height = static_cast<FLOAT>(desc.Height), .MaxDepth = 1.f};
    native->OMSetRenderTargets(1, target_view.GetAddressOf(), nullptr);
    native->RSSetViewports(1, &viewport);
    native->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    native->VSSetShader(full_screen.Get(), nullptr, 0);
    native->PSSetShader(green.Get(), nullptr, 0);
    native->PSSetShaderResources(0, 1, kept_view.GetAddressOf());
    native->Draw(3, 0);
    ID3D11ShaderResourceView* const none = nullptr;
    native->PSSetShaderResources(0, 1, &none);
    native->OMSetRenderTargets(0, nullptr, nullptr);
    traced_size = {width, height};
    return true;
  });
  const auto run = [&](uint64_t first, uint64_t frames) {
    for (uint64_t frame = first; frame < first + frames; ++frame) {
      context->ClearRenderTargetView(target.rtv.Get(), ZERO.data());
#ifdef UE_GAME_ROME
      talesofarise::ue_light_bounce::Apply(context.Get(), inputs, talesofarise::ue_light_bounce::ConstantsOf(sliders, frame), true, target.rtv.Get());
#else
      ID3D11ShaderResourceView* irradiance =
          talesofarise::ue_light_bounce::Apply(context.Get(), inputs, talesofarise::ue_light_bounce::ConstantsOf(sliders, frame), true, nullptr);
      REQUIRE(irradiance != nullptr);
      ComPtr<ID3D11Resource> resource;
      irradiance->GetResource(resource.GetAddressOf());
      bounce = Read(resource.Get(), 4);
#endif
    }
#ifdef UE_GAME_ROME
    bounce = Read(target.texture.Get(), 4);
#endif
  };
  run(16, 32);
  const float rays_green = mean(-1.4f, -0.6f, 1);
  const float rays_red = mean(-1.4f, -0.6f, 0);
  trace_rays = false;
  run(48, 16);
  twattila::light_bounce::SetRayTracedTrace(nullptr);
  const float back_red = mean(-1.4f, -0.6f, 0);
  std::printf("GT-VBGI with the rays' trace: ground beside the wall green %.4f red %.4f (traced %u x %u); its own trace again: red %.4f\n",
              rays_green, rays_red, traced_size[0], traced_size[1], back_red);
  CHECK(rays_green > 0.08f);
  CHECK(rays_red < 0.25f * rays_green);
  CHECK(rays_green > 10.f * near_green);
  CHECK(traced_size[0] == (WIDTH + 3) / 4 && traced_size[1] == (HEIGHT + 3) / 4);
  CHECK(back_red > 0.05f);

  // Strength 0: nothing drawn.
  const std::array<FLOAT, 4> mark = {0.25f, 0.5f, 0.75f, 1.f};
  context->ClearRenderTargetView(target.rtv.Get(), mark.data());
  twattila::light_bounce::Sliders off = sliders;
  off.strength = 0.f;
  CHECK(talesofarise::ue_light_bounce::Apply(context.Get(), inputs, talesofarise::ue_light_bounce::ConstantsOf(off, 16), true, target.rtv.Get()) == nullptr);
  CHECK(talesofarise::ue_light_bounce::Apply(context.Get(), inputs, talesofarise::ue_light_bounce::ConstantsOf(off, 16), true, nullptr) == nullptr);
  const auto untouched = Read(target.texture.Get(), 4);
  bool same = true;
  for (size_t index = 0; index < untouched.size(); ++index) same &= untouched[index] == mark[index % 4];
  CHECK(same);
}

// ---- 3. Tales: contactshadow00 -----------------------------------------------------------------------------------------

#ifndef UE_GAME_ROME
static ComPtr<ID3DBlob> Compile(const std::string& source, const char* target) {
  ComPtr<ID3DBlob> code;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, "main", target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                        code.GetAddressOf(), errors.GetAddressOf()))) {
    std::printf("compile: %s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
    std::exit(1);
  }
  return code;
}

static void TestContactShadows(const char* dumped_path) {
  std::ifstream file(dumped_path, std::ios::binary);
  const std::vector<uint8_t> dumped = {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  REQUIRE(!dumped.empty());
  // The full-screen triangle with Unreal's screen vector (the translated world direction a scene depth scales).
  char vertex_source[1024];
  std::snprintf(vertex_source, sizeof(vertex_source),
                "void main(uint id : SV_VertexID, out float4 v0 : TEXCOORD0, out float3 v1 : TEXCOORD1, out float4 position : SV_Position) {\n"
                "  const float2 uv = float2((id << 1) & 2, id & 2);\n"
                "  const float2 ndc = uv * float2(2, -2) + float2(-1, 1);\n"
                "  v0 = float4(uv, uv);\n"
                "  v1 = ndc.x / %.9g * float3(%.9g, %.9g, %.9g) + ndc.y / %.9g * float3(%.9g, %.9g, %.9g) + float3(%.9g, %.9g, %.9g);\n"
                "  position = float4(ndc, 0, 1);\n"
                "}\n",
                P00, RIGHT[0], RIGHT[1], RIGHT[2], P11, UP[0], UP[1], UP[2], FORWARD[0], FORWARD[1], FORWARD[2]);
  const auto vertex_code = Compile(vertex_source, "vs_5_0");
  ComPtr<ID3D11VertexShader> vertex_shader;
  ComPtr<ID3D11PixelShader> game_shader;
  ComPtr<ID3D11PixelShader> replacement;
  REQUIRE(SUCCEEDED(device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, vertex_shader.GetAddressOf())));
  REQUIRE(SUCCEEDED(device->CreatePixelShader(dumped.data(), dumped.size(), nullptr, game_shader.GetAddressOf())));
  REQUIRE(SUCCEEDED(device->CreatePixelShader(__0x95802ADA.data(), __0x95802ADA.size(), nullptr, replacement.GetAddressOf())));

  // The box scene (world-space positions: the View's centimetres), every pixel a character's: model 15 left of the
  // middle (the march toward the light), 14 right of it (the second march).
  const auto pixels = Trace(false);
  std::vector<float> depth(pixels.size());
  std::vector<uint8_t> gbuffer_b(pixels.size() * 4, 0);
  std::vector<float> normals(pixels.size() * 4, 0.f);
  for (size_t index = 0; index < pixels.size(); ++index) {
    depth[index] = (pixels[index].surface == SKY) ? 0.f : NEAR_M / pixels[index].depth;
    gbuffer_b[(index * 4) + 3] = ((index % WIDTH) < WIDTH / 2) ? 15 : 14;
    for (int c = 0; c < 3; ++c) normals[(index * 4) + c] = (pixels[index].normal[c] * 0.5f) + 0.5f;
  }
  const Texture depth_texture = MakeTexture(DXGI_FORMAT_R32_FLOAT, depth.data(), WIDTH * 4);
  const Texture normal_texture = MakeTexture(DXGI_FORMAT_R32G32B32A32_FLOAT, normals.data(), WIDTH * 16);
  const Texture material_texture = MakeTexture(DXGI_FORMAT_R8G8B8A8_UNORM, gbuffer_b.data(), WIDTH * 4);
  const Texture output = MakeTexture(DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, true);
  const auto view_rows = ViewRows();
  const auto view = ConstantBuffer(view_rows.data(), static_cast<UINT>(view_rows.size() * sizeof(Row)));
  // Bandai's constants: the march's view-space step away from the light (c42, centimetres; its march toward the light
  // reaches 2.55 of them: here 1.5 m), w a depth bias in centimetres; c43.zw the mask's mix.
  std::array<Row, 44> pass = {};
  const float step = 150.f / 2.55f;
  pass[42] = {-Dot(TOWARD_SUN, RIGHT) * step, -Dot(TOWARD_SUN, UP) * step, -Dot(TOWARD_SUN, FORWARD) * step, 0.5f};
  pass[43] = {0.f, 0.f, 0.25f, 0.5f};
  const auto pass_buffer = ConstantBuffer(pass.data(), sizeof(pass));
  ShaderInjectData injection = {};
  const D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .ComparisonFunc = D3D11_COMPARISON_NEVER, .MaxLOD = D3D11_FLOAT32_MAX};
  ComPtr<ID3D11SamplerState> sampler;
  REQUIRE(SUCCEEDED(device->CreateSamplerState(&sampler_desc, sampler.GetAddressOf())));

  const auto draw = [&](ID3D11PixelShader* shader, float bend) {
    injection.screen_space_shadows = bend;
    const auto injection_buffer = ConstantBuffer(&injection, sizeof(injection));
    const D3D11_VIEWPORT viewport = {.Width = WIDTH, .Height = HEIGHT, .MaxDepth = 1.f};
    ID3D11RenderTargetView* target = output.rtv.Get();
    const std::array<ID3D11ShaderResourceView*, 3> views = {depth_texture.srv.Get(), normal_texture.srv.Get(), material_texture.srv.Get()};
    const std::array<ID3D11SamplerState*, 3> samplers = {sampler.Get(), sampler.Get(), sampler.Get()};
    const std::array<ID3D11Buffer*, 2> buffers = {pass_buffer.Get(), view.Get()};
    context->ClearState();
    context->OMSetRenderTargets(1, &target, nullptr);
    context->RSSetViewports(1, &viewport);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertex_shader.Get(), nullptr, 0);
    context->PSSetShader(shader, nullptr, 0);
    context->PSSetShaderResources(0, 3, views.data());
    context->PSSetSamplers(0, 3, samplers.data());
    context->PSSetConstantBuffers(0, 2, buffers.data());
    context->PSSetConstantBuffers(13, 1, injection_buffer.GetAddressOf());
    context->Draw(3, 0);
    return Read(output.texture.Get(), 4);
  };
  const auto game = draw(game_shader.Get(), 0.f);
  const auto ours = draw(replacement.Get(), 0.f);
  const auto bend = draw(replacement.Get(), 1.f);
  float most = 0.f;
  int bend_differs = 0;
  int shaded = 0;
  for (size_t index = 0; index < game.size(); index += 4) {
    for (size_t c = 0; c < 4; ++c) most = std::max(most, std::abs(game[index + c] - ours[index + c]));
    bend_differs += (std::abs(game[index] - bend[index]) > 1e-3f) ? 1 : 0;
    shaded += (game[index] > 1e-3f) ? 1 : 0;
  }
  std::printf("contactshadow00: game vs replacement (Bend off) at most %.3g apart; %d of %d pixels shaded, %d differ with Bend on\n", most,
              shaded, static_cast<int>(game.size() / 4), bend_differs);
  CHECK(most <= 1e-5f);
  CHECK(shaded > 500);
  CHECK(bend_differs > 100);
}
#endif

int main(int argc, char** argv) {
  REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                                      context.GetAddressOf())));
  TestBend();
  TestBounce();
#ifndef UE_GAME_ROME
  REQUIRE(argc > 1);
  TestContactShadows(argv[1]);
#endif
  std::printf(failures == 0 ? "PASS\n" : "%d FAILED\n", failures);
  return failures == 0 ? 0 : 1;
}
