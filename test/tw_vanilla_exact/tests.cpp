// R13 (hdr-review-rules.md, "Vanilla exact first"): every replaced shader's Vanilla path (Rendering Mode SDR; Off where
// a mod's Vanilla is its own) on WARP against the game's own bytes, on the same inputs.
// vanilla_exact.exe <manifest> [--no-mutants] [--dump <part of a name>]: one shader a line, tab-separated: game, shader,
// the game's cso, the add-on's cso (the build's flags, its reflection kept), settings (name=value,...: the add-on's
// shader_injection members on top of BASE_SETTINGS), constant registers (GameConstants). run.ps1 writes it from its table
// of games.
// Inputs from the game's reflection: each texture by its name (the scene: 64 colours x 48 exposures, tw_divinity1's;
// bloom; depth; the auto exposure's black / white points; an S-curve; masks), each constant by its name (screen size,
// gamma 2.0, a near-identity colour matrix, debug and editor switches off; the rest fixed noise), 4 samples where a
// texture is multisampled; pixel shaders on a full-screen triangle whose outputs are their inputs, vertex shaders on
// points through stream output. The add-on's own bindings: shader_injection from the settings, the rest unbound (its
// features off). Two input sets:
//   exact: bloom textures flat, bright passes (bloom*) at their source's size: the bloom smoothing (audit R6) is exact
//          by construction there;
//   full:  bloom textures varied, bright passes at a quarter of their source.
// Each shader: EXACT (the full set within TOLERANCE of the game's output or of its 8-bit target's clamp), R6 (the exact
// set within, the full set not, a shader that adds or makes bloom: the smoothing's size reported, not a failure), FAIL.
// Control (must fail): the game's bytes with each write to an output reading other components (x <-> y, or the next
// component) against the game's own. Report: the game's float constants (arithmetic only) x 1.02 one at a time, how many the exact set sees.
#include <windows.h>
#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "test/tw_vanilla_exact/mutants.h"

using Microsoft::WRL::ComPtr;
using Float4 = std::array<float, 4>;

#define REQUIRE(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(2); } } while (false)

constexpr UINT WIDTH = 64;
constexpr UINT HEIGHT = 48;
constexpr UINT GREY_ROW = 20;   // the scene's 0.18
constexpr UINT SAMPLES = 4;
constexpr UINT BLOOM_SCALE = 4;  // bloom textures and the full set's bright-pass targets: a quarter of the screen
constexpr UINT VERTICES = 64;    // vertex shaders' points
constexpr UINT LAYERS = 8;       // pixel shaders that read their target's slice (Unreal's LUT builders): slices drawn
// A difference the game's targets can't store: under a quarter of a 10-bit code (relative above 1). Float reordering
// in the recompiled decompile stays below it; the table prints each difference, 0 bit-exact.
constexpr double TOLERANCE = 2.4e-4;
constexpr uint32_t SENTINEL = 0xC0DEDBADu;  // a target texel no draw wrote (discard)

// The add-on's settings for its Vanilla path where it has them: Rendering Mode SDR (Divinity 2: Off), every multiplier at
// the game's 1, Paper White = UI (no scale), Gamma Correction off (on, it re-encodes the in-game gamma for the display
// on purpose: 2.0 to 2.2 in Rome II, Warhammer I / II, Troy, Pharaoh).
constexpr std::pair<std::string_view, float> BASE_SETTINGS[] = {
    {"hdr", 0.f}, {"tonemap_type", 0.f}, {"peak_white_nits", 1000.f}, {"diffuse_white_nits", 203.f},
    {"graphics_white_nits", 203.f}, {"gamma_correction", 0.f}, {"swap_chain_encoding", 0.f}, {"bloom", 1.f},
    {"lensflare", 1.f}, {"lens_flare", 1.f}, {"vignette", 1.f}, {"dof", 1.f}, {"depth_of_field", 1.f}, {"uvdistort", 1.f},
    {"godrays", 1.f}, {"sun_shafts", 1.f}, {"filmgrain", 1.f}, {"film_grain", 1.f}, {"chromaticaberration", 1.f},
    {"chromatic_aberration", 1.f}, {"sharpening", 1.f}, {"exposure", 1.f}, {"highlights", 1.f}, {"shadows", 1.f},
    {"contrast", 1.f}, {"saturation", 1.f}, {"lut", 1.f}, {"ui", 1.f}, {"whiteclip", 1.f}, {"fog_strength", 1.f},
    {"someindicators", 1.f}, {"cone_response_exponent", 1.f}, {"highlight_expansion", 1.f}, {"motion_blur", 1.f},
    {"motion_blur_camera", 1.f}, {"motion_blur_object", 1.f},
};

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;
static bool layer_output = false;  // a vertex shader may write SV_RenderTargetArrayIndex (D3D11.3)

static std::vector<uint8_t> ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static std::string Lower(std::string_view text) {
  std::string lower(text);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return lower;
}

static bool Has(std::string_view text, std::string_view part) { return text.find(part) != std::string_view::npos; }

// Fixed noise in [0, 1).
static float Noise(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
  uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du ^ (d + 1u) * 0x27D4EB2Fu;
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  h ^= h >> 12;
  h *= 0x297A2D39u;
  h ^= h >> 15;
  return static_cast<float>(h >> 8) * (1.f / 16777216.f);
}

static std::array<float, 3> Hue(float hue) {
  const auto channel = [hue](float n) {
    const float k = std::fmod(n + hue * 6.f, 6.f);
    return 1.f - std::max(0.f, std::min({k, 4.f - k, 1.f}));
  };
  return {channel(5.f), channel(3.f), channel(1.f)};
}

// tw_divinity1's scene colours: grey, 48 hues, fire, orange, sky, skin, foliage, near-greys, black.
static std::array<float, 3> SceneColour(UINT column) {
  if (column == 0) return {1.f, 1.f, 1.f};
  if (column <= 48) return Hue((column - 1) / 48.f);
  constexpr std::array<std::array<float, 3>, 5> NAMED = {{{1.f, 0.4f, 0.1f}, {1.f, 0.6f, 0.2f}, {0.35f, 0.6f, 1.f}, {0.8f, 0.55f, 0.4f}, {0.2f, 0.5f, 0.1f}}};
  if (column <= 53) return NAMED[column - 49];
  if (column < 63) return {0.5f + 0.05f * (column - 54), 0.5f, 0.5f - 0.05f * (column - 54)};
  return {0.f, 0.f, 0.f};
}

// What a texture holds, by its name (volume textures: a colour LUT).
enum class Kind { SCENE, BLOOM, FLAT, MASK, DEPTH, POINTS, CURVE, EXPOSURE, LUT };

static Kind KindOf(std::string_view name, bool full) {
  const std::string lower = Lower(name);
  if (Has(lower, "black_and_white") || Has(lower, "black_white")) return Kind::POINTS;
  if (Has(lower, "curve")) return Kind::CURVE;
  if (Has(lower, "exposure") || Has(lower, "adapt")) return Kind::EXPOSURE;
  if (Has(lower, "bloom") || Has(lower, "glow")) return full ? Kind::BLOOM : Kind::FLAT;
  if (Has(lower, "depth") || Has(lower, "gbuffer_channel_4")) return Kind::DEPTH;
  if (Has(lower, "lummap")) return Kind::EXPOSURE;
  if (Has(lower, "hdr") || Has(lower, "frame") || Has(lower, "scene") || Has(lower, "god_ray") || Has(lower, "input") || Has(lower, "basemap")
      || Has(lower, "colour") || Has(lower, "color")) {
    return Kind::SCENE;
  }
  return Kind::MASK;
}

// One texel (x, y of a width x height texture, z its slice, face or sample), linear.
static Float4 Texel(Kind kind, UINT x, UINT y, UINT z, UINT width, UINT height) {
  switch (kind) {
    case Kind::SCENE:
    case Kind::BLOOM: {
      const UINT column = x * WIDTH / width;
      const UINT row = y * HEIGHT / height;
      const auto colour = SceneColour((column + 7 * z) % WIDTH);
      float scale = 0.18f * std::exp2((static_cast<float>(row) - GREY_ROW) * 0.4f);
      if (kind == Kind::BLOOM) scale *= 0.1f + 0.4f * Noise(x, y, z, 11);
      // Alpha: the luminance (Warscape's bright passes read the scene's as such), x 0.5 - 1.5.
      const float luminance = (0.2126f * colour[0] + 0.7152f * colour[1] + 0.0722f * colour[2]) * scale;
      return {colour[0] * scale, colour[1] * scale, colour[2] * scale, luminance * (0.5f + Noise(x, y, z, 3))};
    }
    case Kind::FLAT:
      return {0.6f, 0.45f, 0.3f, 0.5f};
    case Kind::DEPTH: {
      const float depth = 0.1f + 0.4f * (1.f + std::sin(0.11f * x + 0.07f * y + 0.3f * z));
      return {depth, depth, depth, 1.f};
    }
    case Kind::POINTS:
      return {0.035f, 2.4f, 0.5f, 1.f};
    case Kind::CURVE: {
      const float u = (x + 0.5f) / width;
      const float s = u * u * (3.f - 2.f * u);
      return {u + (s - u) * 0.6f, u + (s - u) * 0.7f, u + (s - u) * 0.8f, 1.f};
    }
    case Kind::EXPOSURE:
      return {1.25f, 0.5f, 0.75f, 0.1f};
    case Kind::LUT:
      // Near identity, slightly warm; alpha 0 as the game's SDR LUT (the Unreal and Divinity 2 add-ons tag their HDR
      // LUT's alpha 1).
      return {std::pow(x / (width - 1.f), 0.95f), y / (height - 1.f), std::pow(z / (width - 1.f), 1.05f), 0.f};
    case Kind::MASK:
    default: {
      Float4 texel = {};
      for (UINT c = 0; c < 4; ++c) {
        texel[c] = 0.5f + 0.4f * std::sin(0.37f * x + 1.1f * c + z) * std::cos(0.23f * y + 0.7f * c) + 0.05f * Noise(x, y, z, c);
      }
      return texel;
    }
  }
}

struct Shader {
  std::vector<uint8_t> code;
  ComPtr<ID3D11ShaderReflection> reflection;
  bool vertex = false;
};

static Shader LoadShader(const std::filesystem::path& path) {
  Shader shader{.code = ReadFile(path)};
  if (shader.code.empty() || FAILED(D3DReflect(shader.code.data(), shader.code.size(), IID_PPV_ARGS(&shader.reflection)))) return {};
  D3D11_SHADER_DESC desc = {};
  shader.reflection->GetDesc(&desc);
  shader.vertex = D3D11_SHVER_GET_TYPE(desc.Version) == D3D11_SHVER_VERTEX_SHADER;
  return shader;
}

static std::vector<D3D11_SHADER_INPUT_BIND_DESC> Bindings(ID3D11ShaderReflection* reflection) {
  D3D11_SHADER_DESC desc = {};
  reflection->GetDesc(&desc);
  std::vector<D3D11_SHADER_INPUT_BIND_DESC> bindings(desc.BoundResources);
  for (UINT i = 0; i < desc.BoundResources; ++i) reflection->GetResourceBindingDesc(i, &bindings[i]);
  return bindings;
}

static std::vector<D3D11_SIGNATURE_PARAMETER_DESC> Signature(ID3D11ShaderReflection* reflection, bool outputs) {
  D3D11_SHADER_DESC desc = {};
  reflection->GetDesc(&desc);
  std::vector<D3D11_SIGNATURE_PARAMETER_DESC> elements(outputs ? desc.OutputParameters : desc.InputParameters);
  for (UINT i = 0; i < elements.size(); ++i) {
    if (outputs) {
      reflection->GetOutputParameterDesc(i, &elements[i]);
    } else {
      reflection->GetInputParameterDesc(i, &elements[i]);
    }
  }
  return elements;
}

static bool IsConstant(D3D_SHADER_INPUT_TYPE type) { return type == D3D_SIT_CBUFFER; }
static bool IsView(D3D_SHADER_INPUT_TYPE type) {
  return type == D3D_SIT_TEXTURE || type == D3D_SIT_STRUCTURED || type == D3D_SIT_BYTEADDRESS || type == D3D_SIT_TBUFFER;
}

// A binding's register class: 0 constant buffer, 1 view, 2 sampler, 3 other (UAV).
static int SlotClass(D3D_SHADER_INPUT_TYPE type) { return IsConstant(type) ? 0 : (IsView(type) ? 1 : (type == D3D_SIT_SAMPLER ? 2 : 3)); }

// The (class, slot)s a program declares (dcl_constantbuffer, dcl_resource*, dcl_sampler): Unreal strips its shaders'
// reflection.
static std::vector<std::pair<int, UINT>> DeclaredSlots(const std::vector<uint8_t>& code) {
  const twattila::dxbc::Program program = twattila::dxbc::Read(code);
  std::vector<std::pair<int, UINT>> slots;
  if (program.tokens.empty()) return slots;
  size_t temps_at = 0;
  const size_t first = twattila::dxbc::internal::FirstInstruction(program.tokens, &temps_at);
  for (size_t at = 2; at < first;) {
    const size_t length = twattila::dxbc::Length(program.tokens, at);
    if (length == 0) break;
    const uint32_t opcode = program.tokens[at] & 0x7FF;
    const int slot_class = (opcode == 89) ? 0 : ((opcode == 88 || opcode == 161 || opcode == 162) ? 1 : (opcode == 90 ? 2 : -1));
    twattila::dxbc::Operand operand;
    if (slot_class >= 0
        && twattila::dxbc::ParseOperand(program.tokens, twattila::dxbc::internal::FirstOperand(program.tokens, at), at + length, &operand) != 0) {
      slots.emplace_back(slot_class, operand.index[0]);
    }
    at += length;
  }
  return slots;
}

// The draw's sizes: the screen the game's constants describe, the target the draw writes.
struct Frame {
  UINT screen_width = WIDTH, screen_height = HEIGHT, target_width = WIDTH, target_height = HEIGHT, samples = 1;
  bool full = false;
};

static ComPtr<ID3D11ShaderResourceView> UploadTexture(const D3D11_SHADER_INPUT_BIND_DESC& bind, Kind kind, UINT width, UINT height,
                                                      UINT depth, std::string* note) {
  const bool integer = bind.ReturnType == D3D_RETURN_TYPE_UINT || bind.ReturnType == D3D_RETURN_TYPE_SINT;
  const DXGI_FORMAT format = (bind.ReturnType == D3D_RETURN_TYPE_UINT)   ? DXGI_FORMAT_R32G32B32A32_UINT
                             : (bind.ReturnType == D3D_RETURN_TYPE_SINT) ? DXGI_FORMAT_R32G32B32A32_SINT
                                                                         : DXGI_FORMAT_R32G32B32A32_FLOAT;
  std::vector<uint32_t> words;
  for (UINT z = 0; z < depth; ++z) {
    for (UINT y = 0; y < height; ++y) {
      for (UINT x = 0; x < width; ++x) {
        const Float4 texel = Texel(kind, x, y, z, width, height);
        for (UINT c = 0; c < 4; ++c) words.push_back(integer ? static_cast<uint32_t>(Noise(x, y, z, c) * 256.f) : std::bit_cast<uint32_t>(texel[c]));
      }
    }
  }
  ComPtr<ID3D11ShaderResourceView> view;
  const UINT pitch = width * 16;
  switch (bind.Dimension) {
    case D3D_SRV_DIMENSION_TEXTURE1D: {
      const D3D11_TEXTURE1D_DESC desc = {width, 1, 1, format, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
      const D3D11_SUBRESOURCE_DATA init = {words.data(), 0, 0};
      ComPtr<ID3D11Texture1D> texture;
      REQUIRE(SUCCEEDED(device->CreateTexture1D(&desc, &init, &texture)));
      REQUIRE(SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &view)));
      break;
    }
    case D3D_SRV_DIMENSION_TEXTURE2D:
    case D3D_SRV_DIMENSION_TEXTURE2DARRAY:
    case D3D_SRV_DIMENSION_TEXTURECUBE: {
      const bool cube = bind.Dimension == D3D_SRV_DIMENSION_TEXTURECUBE;
      const D3D11_TEXTURE2D_DESC desc = {width, height, 1, depth, format, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0,
                                         cube ? static_cast<UINT>(D3D11_RESOURCE_MISC_TEXTURECUBE) : 0u};
      std::vector<D3D11_SUBRESOURCE_DATA> init(depth);
      for (UINT z = 0; z < depth; ++z) init[z] = {words.data() + size_t{z} * width * height * 4, pitch, 0};
      ComPtr<ID3D11Texture2D> texture;
      REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, init.data(), &texture)));
      D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {format, D3D11_SRV_DIMENSION_TEXTURECUBE};
      view_desc.TextureCube = {0, 1};
      REQUIRE(SUCCEEDED(device->CreateShaderResourceView(texture.Get(), cube ? &view_desc : nullptr, &view)));
      break;
    }
    case D3D_SRV_DIMENSION_TEXTURE3D: {
      const D3D11_TEXTURE3D_DESC desc = {width, height, depth, 1, format, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
      const D3D11_SUBRESOURCE_DATA init = {words.data(), pitch, pitch * height};
      ComPtr<ID3D11Texture3D> texture;
      REQUIRE(SUCCEEDED(device->CreateTexture3D(&desc, &init, &texture)));
      REQUIRE(SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &view)));
      break;
    }
    default:
      *note += std::string(" unbound:") + bind.Name;
  }
  return view;
}

// A multisampled texture: its samples drawn from a texture array of one slice a sample.
static ComPtr<ID3D11ShaderResourceView> MultisampledTexture(const D3D11_SHADER_INPUT_BIND_DESC& bind, Kind kind) {
  std::string unused;
  D3D11_SHADER_INPUT_BIND_DESC array_bind = bind;
  array_bind.Dimension = D3D_SRV_DIMENSION_TEXTURE2DARRAY;
  array_bind.ReturnType = D3D_RETURN_TYPE_FLOAT;
  const auto slices = UploadTexture(array_bind, kind, WIDTH, HEIGHT, SAMPLES, &unused);
  // 32-bit float where the device multisamples it, else 16-bit.
  UINT levels = 0;
  device->CheckMultisampleQualityLevels(DXGI_FORMAT_R32G32B32A32_FLOAT, SAMPLES, &levels);
  const DXGI_FORMAT format = (levels > 0) ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT;
  const D3D11_TEXTURE2D_DESC desc = {WIDTH, HEIGHT, 1, 1, format, {SAMPLES, 0}, D3D11_USAGE_DEFAULT,
                                     D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0, 0};
  ComPtr<ID3D11Texture2D> texture;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)));
  ComPtr<ID3D11RenderTargetView> target;
  REQUIRE(SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, &target)));
  static ComPtr<ID3D11VertexShader> vertex;
  static ComPtr<ID3D11PixelShader> pixel;
  if (!pixel) {
    constexpr char VERTEX[] = "float4 main(uint id : SV_VertexID) : SV_Position { float2 uv = float2((id << 1) & 2, id & 2);"
                              " return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); }";
    constexpr char PIXEL[] = "Texture2DArray<float4> t : register(t0);"
                             "float4 main(float4 p : SV_Position, uint s : SV_SampleIndex) : SV_Target { return t.Load(int4(p.xy, s, 0)); }";
    ComPtr<ID3DBlob> code;
    REQUIRE(SUCCEEDED(D3DCompile(VERTEX, sizeof(VERTEX) - 1, nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &code, nullptr)));
    REQUIRE(SUCCEEDED(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vertex)));
    REQUIRE(SUCCEEDED(D3DCompile(PIXEL, sizeof(PIXEL) - 1, nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &code, nullptr)));
    REQUIRE(SUCCEEDED(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &pixel)));
  }
  context->ClearState();
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(vertex.Get(), nullptr, 0);
  context->PSSetShader(pixel.Get(), nullptr, 0);
  context->PSSetShaderResources(0, 1, slices.GetAddressOf());
  const D3D11_VIEWPORT viewport = {0.f, 0.f, WIDTH, HEIGHT, 0.f, 1.f};
  context->RSSetViewports(1, &viewport);
  context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
  context->Draw(3, 0);
  context->ClearState();
  ComPtr<ID3D11ShaderResourceView> view;
  REQUIRE(SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &view)));
  return view;
}

static ComPtr<ID3D11ShaderResourceView> MakeBuffer(const D3D11_SHADER_INPUT_BIND_DESC& bind, Kind kind) {
  const bool structured = bind.Type == D3D_SIT_STRUCTURED;
  const UINT stride = structured ? std::max<UINT>(bind.NumSamples, 4) : 16;
  const UINT size = (bind.Type == D3D_SIT_BYTEADDRESS) ? 4096 : stride * VERTICES;
  std::vector<uint32_t> words(size / 4);
  for (UINT i = 0; i < words.size(); ++i) {
    const UINT element = i * 4 / stride;
    const UINT component = i % (stride / 4);
    words[i] = std::bit_cast<uint32_t>(Texel(kind, element, 0, 0, VERTICES, 1)[component % 4]);
  }
  D3D11_BUFFER_DESC desc = {size, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, structured ? stride : 0};
  if (structured) desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
  if (bind.Type == D3D_SIT_BYTEADDRESS) desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
  const D3D11_SUBRESOURCE_DATA init = {words.data(), 0, 0};
  ComPtr<ID3D11Buffer> buffer;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&desc, &init, &buffer)));
  D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {};
  if (bind.Type == D3D_SIT_BYTEADDRESS) {
    view_desc.Format = DXGI_FORMAT_R32_TYPELESS;
    view_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
    view_desc.BufferEx = {0, size / 4, D3D11_BUFFEREX_SRV_FLAG_RAW};
  } else {
    view_desc.Format = structured ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32G32B32A32_FLOAT;
    view_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    view_desc.Buffer = {{0}, {size / stride}};
  }
  ComPtr<ID3D11ShaderResourceView> view;
  REQUIRE(SUCCEEDED(device->CreateShaderResourceView(buffer.Get(), &view_desc, &view)));
  return view;
}

static ComPtr<ID3D11ShaderResourceView> MakeView(const D3D11_SHADER_INPUT_BIND_DESC& bind, const Frame& frame, std::string* note) {
  const Kind kind = (bind.Dimension == D3D_SRV_DIMENSION_TEXTURE3D) ? Kind::LUT : KindOf(bind.Name, frame.full);
  if (bind.Type != D3D_SIT_TEXTURE || bind.Dimension == D3D_SRV_DIMENSION_BUFFER) return MakeBuffer(bind, kind);
  if (bind.Dimension == D3D_SRV_DIMENSION_TEXTURE2DMS) return MultisampledTexture(bind, kind);
  // Masks and noise 64 x 64 (Rome Remastered's dither wraps its coordinates at 64).
  UINT width = WIDTH, height = (kind == Kind::MASK) ? 64 : HEIGHT, depth = 1;
  if (kind == Kind::POINTS || kind == Kind::EXPOSURE) width = height = 1;
  if (kind == Kind::CURVE) width = 256, height = 1;
  if (kind == Kind::BLOOM || kind == Kind::FLAT) width /= BLOOM_SCALE, height /= BLOOM_SCALE;
  if (bind.Dimension == D3D_SRV_DIMENSION_TEXTURE2DARRAY) depth = 4;
  if (bind.Dimension == D3D_SRV_DIMENSION_TEXTURECUBE) width = height = 16, depth = 6;
  if (bind.Dimension == D3D_SRV_DIMENSION_TEXTURE3D) width = height = depth = 16;
  if (bind.Dimension == D3D_SRV_DIMENSION_TEXTURE1D && kind != Kind::CURVE) width = 64;
  return UploadTexture(bind, kind, width, height, depth, note);
}

// A constant by its name: the screen and target sizes, the game's gamma 2.0, a near-identity colour matrix, sane
// auto exposure points; nullopt: noise (floats) or 1 (integers).
static std::optional<std::vector<float>> Override(std::string_view name, const Frame& frame) {
  const auto size = [](float w, float h) { return std::vector<float>{w, h, 1.f / w, 1.f / h}; };
  const float sw = static_cast<float>(frame.screen_width), sh = static_cast<float>(frame.screen_height);
  const float tw = static_cast<float>(frame.target_width), th = static_cast<float>(frame.target_height);
  if (name == "g_screen_size" || name == "screen_size" || name == "g_viewport_dimensions") return size(sw, sh);
  if (name == "g_render_target_dimensions") return size(tw, th);
  if (name == "g_render_target_recip_size") return std::vector<float>{1.f / tw, 1.f / th};
  if (name == "g_viewport_origin" || name == "g_vpos_texel_offset") return std::vector<float>{0.f, 0.f, 0.f, 0.f};
  if (name == "g_num_of_samples") return std::vector<float>{static_cast<float>(frame.samples)};
  if (name == "g_supersampling") return std::vector<float>{1.f};
  if (name == "g_gamma_output") return std::vector<float>{2.f};
  if (name == "g_inv_gamma_output") return std::vector<float>{0.5f};
  if (name == "g_brightness") return std::vector<float>{1.f};  // menu Brightness 10, the default
  if (name == "g_hdr_display") return std::vector<float>{0.f};  // the game's own HDR output off (Shogun 2)
  if (name == "overscan") return std::vector<float>{1.f, 1.f, 1.f, 1.f};  // Warhammer III's render scale: 100%
  if (name == "colour_matrix") {
    return std::vector<float>{1.04f, -0.03f, 0.01f, 0.01f, 0.02f, 0.98f, -0.02f, 0.005f, -0.01f, 0.02f, 1.03f, -0.004f, 0.f, 0.f, 0.f, 1.f};
  }
  if (Has(name, "time")) return std::vector<float>{3.25f};
  if (name == "g_luma_black_point" || name == "g_black_point") return std::vector<float>{0.035f};
  if (name == "g_luma_white_point" || name == "g_white_point") return std::vector<float>{2.4f};
  if (name == "g_log_Y_black_point") return std::vector<float>{std::log(0.035f)};
  if (name == "g_log_Y_white_point") return std::vector<float>{std::log(2.4f)};
  if (name == "g_whiteout_start") return std::vector<float>{1.5f};
  if (name == "g_whiteout_end") return std::vector<float>{4.f};
  // Shogun 2's tone map (data.pack's TONE_MAPPING) and its colour grading near neutral.
  if (name == "g_tone_mapping_film_slope") return std::vector<float>{0.88f};
  if (name == "g_tone_mapping_film_toe") return std::vector<float>{0.5f};
  if (name == "g_tone_mapping_film_shoulder") return std::vector<float>{0.135f};
  if (name == "g_tone_mapping_film_black_clip") return std::vector<float>{0.f};
  if (name == "g_tone_mapping_film_white_clip") return std::vector<float>{0.04f};
  if (name == "g_tone_mapping_brightness" || name == "g_tone_mapping_brightness_fade") return std::vector<float>{1.1f};
  if (name == "cg_color_temp") return std::vector<float>{6200.f};
  if (name.starts_with("cg_") && (name.ends_with("_saturation") || name.ends_with("_contrast") || name.ends_with("_gamma") || name.ends_with("_gain"))) {
    return std::vector<float>{1.03f, 0.98f, 1.01f, 1.02f};
  }
  if (name.starts_with("cg_") && name.ends_with("_offset")) return std::vector<float>{0.004f, -0.002f, 0.003f, 0.f};
  if (name == "cg_shadows_max") return std::vector<float>{0.09f};
  if (name == "cg_mids_max") return std::vector<float>{0.5f};
  if (name == "cg_highlights_max") return std::vector<float>{0.5f};
  return std::nullopt;
}

static ComPtr<ID3D11Buffer> ConstantBuffer(const std::vector<uint32_t>& words) {
  const D3D11_BUFFER_DESC desc = {static_cast<UINT>((words.size() * 4 + 15) / 16 * 16), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
  std::vector<uint32_t> padded(desc.ByteWidth / 4);
  std::copy(words.begin(), words.end(), padded.begin());
  const D3D11_SUBRESOURCE_DATA init = {padded.data(), 0, 0};
  ComPtr<ID3D11Buffer> buffer;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&desc, &init, &buffer)));
  return buffer;
}

// The game's constant buffer: fixed noise (0.25 - 0.75), integers 1, debug / editor / disable switches 0, Override, then
// the manifest's registers for buffers without names ("cb0[1]=2.2,1,1,0.1;...", u: an integer; Feral's Rome Remastered).
static std::vector<uint32_t> GameConstants(ID3D11ShaderReflection* reflection, const char* name, UINT slot, const Frame& frame,
                                           std::string_view registers) {
  ID3D11ShaderReflectionConstantBuffer* buffer = reflection->GetConstantBufferByName(name);
  D3D11_SHADER_BUFFER_DESC desc = {};
  if (FAILED(buffer->GetDesc(&desc))) return {};
  std::vector<uint32_t> words(desc.Size / 4);
  for (UINT i = 0; i < words.size(); ++i) words[i] = std::bit_cast<uint32_t>(0.25f + 0.5f * Noise(slot, i, 7, 0));
  for (UINT v = 0; v < desc.Variables; ++v) {
    ID3D11ShaderReflectionVariable* variable = buffer->GetVariableByIndex(v);
    D3D11_SHADER_VARIABLE_DESC variable_desc = {};
    D3D11_SHADER_TYPE_DESC type_desc = {};
    variable->GetDesc(&variable_desc);
    variable->GetType()->GetDesc(&type_desc);
    const UINT first = variable_desc.StartOffset / 4;
    const UINT count = std::min<UINT>(variable_desc.Size / 4, static_cast<UINT>(words.size()) - first);
    const bool integer = type_desc.Type == D3D_SVT_INT || type_desc.Type == D3D_SVT_UINT || type_desc.Type == D3D_SVT_BOOL;
    const std::string lower = Lower(variable_desc.Name);
    const bool off = Has(lower, "debug") || Has(lower, "editor") || Has(lower, "disable");
    const auto values = Override(variable_desc.Name, frame);
    for (UINT i = 0; i < count; ++i) {
      if (off) {
        words[first + i] = 0;
      } else if (values && i < values->size()) {
        words[first + i] = integer ? static_cast<uint32_t>((*values)[i]) : std::bit_cast<uint32_t>((*values)[i]);
      } else if (integer) {
        words[first + i] = 1;
      }
    }
  }
  std::stringstream entries{std::string(registers)};
  std::string entry;
  while (std::getline(entries, entry, ';')) {
    const size_t open = entry.find('[');
    const size_t equals = entry.find("]=");
    if (open == std::string::npos || equals == std::string::npos || entry.substr(0, open) != name) continue;
    size_t at = std::stoul(entry.substr(open + 1, equals - open - 1)) * 4;
    std::stringstream values(entry.substr(equals + 2));
    std::string value;
    for (; std::getline(values, value, ',') && at < words.size(); ++at) {
      words[at] = (value[0] == 'u') ? static_cast<uint32_t>(std::stoul(value.substr(1))) : std::bit_cast<uint32_t>(std::stof(value));
    }
  }
  return words;
}

using Settings = std::vector<std::pair<std::string, float>>;

// The add-on's shader_injection: every member by name from `settings`, the rest 0.
static std::vector<uint32_t> InjectionConstants(ID3D11ShaderReflection* reflection, const char* name, const Settings& settings) {
  ID3D11ShaderReflectionConstantBuffer* buffer = reflection->GetConstantBufferByName(name);
  D3D11_SHADER_BUFFER_DESC desc = {};
  if (FAILED(buffer->GetDesc(&desc)) || desc.Variables == 0) return {};
  std::vector<uint32_t> words(desc.Size / 4);
  ID3D11ShaderReflectionVariable* variable = buffer->GetVariableByIndex(0);
  D3D11_SHADER_VARIABLE_DESC variable_desc = {};
  variable->GetDesc(&variable_desc);
  ID3D11ShaderReflectionType* type = variable->GetType();
  D3D11_SHADER_TYPE_DESC type_desc = {};
  type->GetDesc(&type_desc);
  for (UINT m = 0; m < type_desc.Members; ++m) {
    D3D11_SHADER_TYPE_DESC member = {};
    type->GetMemberTypeByIndex(m)->GetDesc(&member);
    const std::string_view member_name = type->GetMemberTypeName(m);
    for (const auto& [key, value] : settings) {
      const UINT at = (variable_desc.StartOffset + member.Offset) / 4;
      if (key == member_name && at < words.size()) words[at] = std::bit_cast<uint32_t>(value);
    }
  }
  return words;
}

// Everything one draw binds, for both shaders alike.
struct Setup {
  Frame frame;
  std::vector<std::pair<UINT, ComPtr<ID3D11ShaderResourceView>>> views;
  std::vector<std::pair<UINT, ComPtr<ID3D11Buffer>>> constants;
  std::vector<std::pair<UINT, ComPtr<ID3D11SamplerState>>> samplers;
  std::string note;
  bool bloom = false;  // a texture of the bloom (or glow) bound
};

static Setup MakeSetup(const Shader& game, const Shader& addon, const Settings& settings, std::string_view registers, bool full,
                       bool bright_pass) {
  Setup setup;
  setup.frame.full = full;
  // The game's bindings: its reflection's, else the add-on's at the slots the game's program declares (with that
  // reflection's names and layouts).
  std::vector<std::pair<D3D11_SHADER_INPUT_BIND_DESC, ID3D11ShaderReflection*>> game_bindings;
  for (const auto& bind : Bindings(game.reflection.Get())) game_bindings.emplace_back(bind, game.reflection.Get());
  if (game_bindings.empty()) {
    const auto declared = DeclaredSlots(game.code);
    for (const auto& bind : Bindings(addon.reflection.Get())) {
      if (std::find(declared.begin(), declared.end(), std::pair(SlotClass(bind.Type), bind.BindPoint)) != declared.end()) {
        game_bindings.emplace_back(bind, addon.reflection.Get());
      }
    }
  }
  for (const auto& [bind, unused] : game_bindings) {
    if (bind.Type == D3D_SIT_TEXTURE && bind.Dimension == D3D_SRV_DIMENSION_TEXTURE2DMS) setup.frame.samples = SAMPLES;
    if (IsView(bind.Type) && KindOf(bind.Name, true) == Kind::BLOOM) setup.bloom = true;
  }
  if (bright_pass && full) setup.frame.target_width = WIDTH / BLOOM_SCALE, setup.frame.target_height = HEIGHT / BLOOM_SCALE;
  for (const auto& [bind, reflection] : game_bindings) {
    for (UINT slot = bind.BindPoint; slot < bind.BindPoint + std::max<UINT>(bind.BindCount, 1); ++slot) {
      if (IsConstant(bind.Type)) {
        setup.constants.emplace_back(slot, ConstantBuffer(GameConstants(reflection, bind.Name, slot, setup.frame, registers)));
      } else if (IsView(bind.Type)) {
        setup.views.emplace_back(slot, MakeView(bind, setup.frame, &setup.note));
      } else if (bind.Type == D3D_SIT_SAMPLER) {
        const bool compare = (bind.uFlags & D3D_SIF_COMPARISON_SAMPLER) != 0;
        const bool point = Has(Lower(bind.Name), "point");
        const D3D11_FILTER filter = compare ? (point ? D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT : D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR)
                                            : (point ? D3D11_FILTER_MIN_MAG_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_LINEAR);
        const D3D11_SAMPLER_DESC desc = {filter, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, 0, 1,
                                         compare ? D3D11_COMPARISON_LESS_EQUAL : D3D11_COMPARISON_NEVER, {}, 0, D3D11_FLOAT32_MAX};
        ComPtr<ID3D11SamplerState> sampler;
        REQUIRE(SUCCEEDED(device->CreateSamplerState(&desc, &sampler)));
        setup.samplers.emplace_back(slot, sampler);
      } else {
        setup.note += std::string(" unbound:") + bind.Name;
      }
    }
  }
  // The add-on's own: shader_injection; the rest (render scale, debug views) unbound as with those features off.
  for (const auto& bind : Bindings(addon.reflection.Get())) {
    const bool shared = std::any_of(game_bindings.begin(), game_bindings.end(), [&](const auto& other) {
      return other.first.BindPoint == bind.BindPoint && SlotClass(other.first.Type) == SlotClass(bind.Type);
    });
    if (shared) continue;
    if (IsConstant(bind.Type) && std::string_view(bind.Name) == "shader_injection") {
      std::vector<std::pair<std::string, float>> all;
      for (const auto& [key, value] : BASE_SETTINGS) all.emplace_back(std::string(key), value);
      all.insert(all.end(), settings.begin(), settings.end());  // later entries win (InjectionConstants writes in order)
      setup.constants.emplace_back(bind.BindPoint, ConstantBuffer(InjectionConstants(addon.reflection.Get(), bind.Name, all)));
    } else if (std::string_view(bind.Name) != "render_scale") {
      setup.note += std::string(" addon-only:") + bind.Name;
    }
  }
  return setup;
}

static void Bind(const Setup& setup, bool vertex_stage) {
  for (const auto& [slot, view] : setup.views) {
    if (vertex_stage) {
      context->VSSetShaderResources(slot, 1, view.GetAddressOf());
    } else {
      context->PSSetShaderResources(slot, 1, view.GetAddressOf());
    }
  }
  for (const auto& [slot, buffer] : setup.constants) {
    if (vertex_stage) {
      context->VSSetConstantBuffers(slot, 1, buffer.GetAddressOf());
    } else {
      context->PSSetConstantBuffers(slot, 1, buffer.GetAddressOf());
    }
  }
  for (const auto& [slot, sampler] : setup.samplers) {
    if (vertex_stage) {
      context->VSSetSamplers(slot, 1, sampler.GetAddressOf());
    } else {
      context->PSSetSamplers(slot, 1, sampler.GetAddressOf());
    }
  }
}

// LAYERS slices where the pixel shader reads its target's slice and the device lets a vertex shader write it, else 1.
static UINT Layers(ID3D11ShaderReflection* pixel) {
  const auto inputs = Signature(pixel, false);
  const bool layered = std::any_of(inputs.begin(), inputs.end(), [](const auto& e) { return e.SystemValueType == D3D_NAME_RENDER_TARGET_ARRAY_INDEX; });
  return (layered && layer_output) ? LAYERS : 1;
}

// A vertex shader whose outputs are `pixel`'s inputs (same order, so the same registers): the full-screen triangle's
// position, the instance as the target's slice, every other input a linear function of the screen's uv (integers
// constant).
static ComPtr<ID3D11VertexShader> VertexShaderFor(ID3D11ShaderReflection* pixel) {
  auto elements = Signature(pixel, false);
  std::sort(elements.begin(), elements.end(), [](const auto& a, const auto& b) {
    return std::pair(a.Register, std::countr_zero(static_cast<unsigned>(a.Mask))) < std::pair(b.Register, std::countr_zero(static_cast<unsigned>(b.Mask)));
  });
  std::string fields;
  std::string writes;
  bool position = false;
  UINT last_register = ~0u;
  UINT next_component = 0;
  for (UINT k = 0; k < elements.size(); ++k) {
    const auto& element = elements[k];
    const std::string field = "f" + std::to_string(k);
    if (element.SystemValueType == D3D_NAME_POSITION) {
      fields += "float4 " + field + " : SV_Position; ";
      writes += "o." + field + " = position; ";
      position = true;
      continue;
    }
    if (element.SystemValueType == D3D_NAME_RENDER_TARGET_ARRAY_INDEX && Layers(pixel) > 1) {
      fields += "uint " + field + " : SV_RenderTargetArrayIndex; ";
      writes += "o." + field + " = instance; ";
      continue;
    }
    if (element.SystemValueType != D3D_NAME_UNDEFINED) continue;  // the rasterizer's (front face, sample index)
    if (element.Register != last_register) next_component = 0, last_register = element.Register;
    const UINT last = 31 - std::countl_zero(static_cast<unsigned>(element.Mask));
    const UINT width = last + 1 - next_component;
    next_component = last + 1;
    const std::string base = (element.ComponentType == D3D_REGISTER_COMPONENT_UINT32)   ? "uint"
                             : (element.ComponentType == D3D_REGISTER_COMPONENT_SINT32) ? "int"
                                                                                        : "float";
    const std::string type = base + (width > 1 ? std::to_string(width) : "");
    const std::string swizzle = std::string(".xyzw").substr(0, width + 1);
    fields += type + " " + field + " : " + element.SemanticName + std::to_string(element.SemanticIndex) + "; ";
    if (base == "float") {
      writes += "o." + field + " = (float4(uv, 1 - 0.5 * uv.x, 0.25 + 0.5 * uv.y) * " + std::to_string(0.9 + 0.1 * k) + " + "
                + std::to_string(0.03 * k) + ")" + swizzle + "; ";
    } else {
      writes += "o." + field + " = " + base + "4(" + std::to_string(k + 1) + ", 2, 3, 0)" + swizzle + "; ";
    }
  }
  if (!position) fields += "float4 position : SV_Position; ", writes += "o.position = position; ";
  const std::string source = "struct O { " + fields + "}; O main(uint id : SV_VertexID, uint instance : SV_InstanceID) { float2 uv = float2((id << 1) & 2, id & 2);"
                             " float4 position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); O o; " + writes + "return o; }";
  ComPtr<ID3DBlob> code;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &code, &errors))) {
    std::printf("vertex shader for the inputs failed: %s\n%s\n", source.c_str(), errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
    return {};
  }
  ComPtr<ID3D11VertexShader> shader;
  REQUIRE(SUCCEEDED(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader)));
  return shader;
}

// A draw's outputs: every target's texels (or the stream output's values), each a float unless `integer`.
struct Output {
  std::vector<std::vector<uint32_t>> words;
  std::vector<bool> integer;
};

// A target's texels (every slice) or a buffer's `rows`.
static std::vector<uint32_t> ReadBack(ID3D11Resource* resource, UINT row_bytes, UINT rows) {
  ComPtr<ID3D11Resource> staging;
  ComPtr<ID3D11Texture2D> texture;
  UINT slices = 1;
  if (SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(&texture)))) {
    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    slices = desc.ArraySize;
    desc.Usage = D3D11_USAGE_STAGING, desc.BindFlags = 0, desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ, desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> copy;
    REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &copy)));
    staging = copy;
  } else {
    const D3D11_BUFFER_DESC desc = {row_bytes * rows, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0};
    ComPtr<ID3D11Buffer> copy;
    REQUIRE(SUCCEEDED(device->CreateBuffer(&desc, nullptr, &copy)));
    staging = copy;
  }
  context->CopyResource(staging.Get(), resource);
  std::vector<uint32_t> words(row_bytes / 4 * rows * slices);
  for (UINT slice = 0; slice < slices; ++slice) {
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    REQUIRE(SUCCEEDED(context->Map(staging.Get(), slice, D3D11_MAP_READ, 0, &mapped)));
    for (UINT row = 0; row < rows; ++row) {
      std::memcpy(words.data() + row_bytes / 4 * (rows * slice + row),
                  static_cast<const uint8_t*>(mapped.pData) + size_t{row} * (texture ? mapped.RowPitch : row_bytes), row_bytes);
    }
    context->Unmap(staging.Get(), slice);
  }
  return words;
}

static Output DrawPixel(const std::vector<uint8_t>& code, ID3D11ShaderReflection* game, ID3D11VertexShader* vertex, const Setup& setup) {
  ComPtr<ID3D11PixelShader> pixel;
  if (FAILED(device->CreatePixelShader(code.data(), code.size(), nullptr, &pixel))) return {};
  Output output;
  std::vector<ComPtr<ID3D11Texture2D>> textures;
  std::array<ID3D11RenderTargetView*, 8> views = {};
  std::vector<ComPtr<ID3D11RenderTargetView>> keep;
  UINT count = 0;
  for (const auto& element : Signature(game, true)) {
    if (element.SystemValueType != D3D_NAME_TARGET || element.SemanticIndex >= 8) continue;
    const bool integer = element.ComponentType != D3D_REGISTER_COMPONENT_FLOAT32;
    const DXGI_FORMAT format = (element.ComponentType == D3D_REGISTER_COMPONENT_UINT32)   ? DXGI_FORMAT_R32G32B32A32_UINT
                               : (element.ComponentType == D3D_REGISTER_COMPONENT_SINT32) ? DXGI_FORMAT_R32G32B32A32_SINT
                                                                                          : DXGI_FORMAT_R32G32B32A32_FLOAT;
    const D3D11_TEXTURE2D_DESC desc = {setup.frame.target_width, setup.frame.target_height, 1, Layers(game), format, {1, 0}, D3D11_USAGE_DEFAULT,
                                       D3D11_BIND_RENDER_TARGET, 0, 0};
    ComPtr<ID3D11Texture2D> texture;
    REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)));
    ComPtr<ID3D11RenderTargetView> view;
    REQUIRE(SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, &view)));
    // Float targets start at SENTINEL's bits (a discarded pixel keeps them), integer ones at 0.
    const float clear = integer ? 0.f : std::bit_cast<float>(SENTINEL);
    const std::array<float, 4> colour = {clear, clear, clear, clear};
    context->ClearRenderTargetView(view.Get(), colour.data());
    views[element.SemanticIndex] = view.Get();
    count = std::max(count, element.SemanticIndex + 1);
    textures.push_back(texture);
    keep.push_back(view);
    output.integer.push_back(integer);
  }
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(vertex, nullptr, 0);
  context->PSSetShader(pixel.Get(), nullptr, 0);
  Bind(setup, false);
  const D3D11_VIEWPORT viewport = {0.f, 0.f, static_cast<float>(setup.frame.target_width), static_cast<float>(setup.frame.target_height), 0.f, 1.f};
  context->RSSetViewports(1, &viewport);
  context->OMSetRenderTargets(count, views.data(), nullptr);
  context->DrawInstanced(3, Layers(game), 0, 0);
  context->ClearState();
  for (const auto& texture : textures) output.words.push_back(ReadBack(texture.Get(), setup.frame.target_width * 16, setup.frame.target_height));
  return output;
}

// Vertex shaders: VERTICES points of fixed noise through stream output, every output of the game's signature.
static Output DrawVertex(const std::vector<uint8_t>& code, const Shader& game, const Setup& setup) {
  ComPtr<ID3D11VertexShader> vertex;
  if (FAILED(device->CreateVertexShader(code.data(), code.size(), nullptr, &vertex))) return {};
  std::vector<D3D11_INPUT_ELEMENT_DESC> layout;
  UINT stride = 0;
  const auto inputs = Signature(game.reflection.Get(), false);
  for (const auto& element : inputs) {
    if (element.SystemValueType != D3D_NAME_UNDEFINED) continue;
    const DXGI_FORMAT format = (element.ComponentType == D3D_REGISTER_COMPONENT_UINT32)   ? DXGI_FORMAT_R32G32B32A32_UINT
                               : (element.ComponentType == D3D_REGISTER_COMPONENT_SINT32) ? DXGI_FORMAT_R32G32B32A32_SINT
                                                                                          : DXGI_FORMAT_R32G32B32A32_FLOAT;
    layout.push_back({element.SemanticName, element.SemanticIndex, format, 0, stride, D3D11_INPUT_PER_VERTEX_DATA, 0});
    stride += 16;
  }
  std::vector<uint32_t> vertices(std::max<UINT>(stride, 16) / 4 * VERTICES);
  for (UINT i = 0; i < vertices.size(); ++i) vertices[i] = std::bit_cast<uint32_t>(Noise(i, 5, 9, 1));
  std::vector<D3D11_SO_DECLARATION_ENTRY> declaration;
  UINT out_stride = 0;
  for (const auto& element : Signature(game.reflection.Get(), true)) {
    const auto first = static_cast<BYTE>(std::countr_zero(static_cast<unsigned>(element.Mask)));
    const auto components = static_cast<BYTE>(std::popcount(static_cast<unsigned>(element.Mask)));
    declaration.push_back({0, element.SemanticName, element.SemanticIndex, first, components, 0});
    out_stride += components * 4;
  }
  ComPtr<ID3D11GeometryShader> stream;
  if (FAILED(device->CreateGeometryShaderWithStreamOutput(code.data(), code.size(), declaration.data(), static_cast<UINT>(declaration.size()),
                                                          &out_stride, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &stream))) {
    return {};
  }
  ComPtr<ID3D11InputLayout> input_layout;
  if (!layout.empty()) REQUIRE(SUCCEEDED(device->CreateInputLayout(layout.data(), static_cast<UINT>(layout.size()), game.code.data(), game.code.size(), &input_layout)));
  const D3D11_BUFFER_DESC vertex_desc = {static_cast<UINT>(vertices.size() * 4), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
  const D3D11_SUBRESOURCE_DATA vertex_init = {vertices.data(), 0, 0};
  ComPtr<ID3D11Buffer> vertex_buffer;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&vertex_desc, &vertex_init, &vertex_buffer)));
  std::vector<uint32_t> sentinel(out_stride / 4 * VERTICES, SENTINEL);
  const D3D11_BUFFER_DESC out_desc = {out_stride * VERTICES, D3D11_USAGE_DEFAULT, D3D11_BIND_STREAM_OUTPUT, 0, 0, 0};
  const D3D11_SUBRESOURCE_DATA out_init = {sentinel.data(), 0, 0};
  ComPtr<ID3D11Buffer> out_buffer;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&out_desc, &out_init, &out_buffer)));
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
  context->IASetInputLayout(input_layout.Get());
  const UINT offset = 0;
  const UINT vertex_stride = std::max<UINT>(stride, 16);
  context->IASetVertexBuffers(0, 1, vertex_buffer.GetAddressOf(), &vertex_stride, &offset);
  context->VSSetShader(vertex.Get(), nullptr, 0);
  context->GSSetShader(stream.Get(), nullptr, 0);
  Bind(setup, true);
  context->SOSetTargets(1, out_buffer.GetAddressOf(), &offset);
  context->Draw(VERTICES, 0);
  context->ClearState();
  Output output;
  output.words.push_back(ReadBack(out_buffer.Get(), out_stride, VERTICES));
  output.integer.push_back(false);
  return output;
}

// The worst difference of `test` from `reference` (the game's): each float relative above 1, the better of the game's
// own value and its 8-bit target's clamp; `clamped`: with both clamped to 0 - 1 (equal there, apart above 1 only).
struct Difference {
  double worst = 0.0;
  double clamped = 0.0;
  size_t outliers = 0;  // texels (4 values) past TOLERANCE
  size_t texels = 0;
  bool valid = false;
};

// Within TOLERANCE, or apart at a few single texels by at most one 8-bit code: a sample that falls between texels, its
// bilinear weight rounded to 1/256 on one side of an ulp of its coordinate and not the other (Shogun 2's heat haze).
static bool Exact(const Difference& difference) {
  return difference.valid
         && (difference.worst <= TOLERANCE || (difference.outliers <= std::max<size_t>(2, difference.texels / 500) && difference.worst <= 1.0 / 255.0));
}

static Difference Compare(const Output& reference, const Output& test) {
  Difference difference;
  if (reference.words.empty() || reference.words.size() != test.words.size()) return difference;
  difference.valid = true;
  for (size_t t = 0; t < reference.words.size(); ++t) {
    if (reference.words[t].size() != test.words[t].size()) return {.valid = false};
    for (size_t i = 0; i < reference.words[t].size(); ++i) {
      if (i % 4 == 0) ++difference.texels;
      const uint32_t a = reference.words[t][i];
      const uint32_t b = test.words[t][i];
      if (a == b) continue;
      double error = INFINITY;
      double clamped = INFINITY;
      const double game = std::bit_cast<float>(a);
      const double ours = std::bit_cast<float>(b);
      if (reference.integer[t] || a == SENTINEL || b == SENTINEL) {
      } else if (std::isnan(game) || std::isnan(ours)) {
        // Both NaN; or the game's NaN, which its 8-bit target stores as 0.
        if (std::isnan(game) == std::isnan(ours)) error = clamped = 0.0;
        if (std::isnan(game) && !std::isnan(ours)) error = clamped = std::abs(ours);
      } else {
        error = std::min(std::abs(ours - game) / std::max(1.0, std::abs(game)), std::abs(ours - std::clamp(game, 0.0, 1.0)));
        clamped = std::abs(std::clamp(ours, 0.0, 1.0) - std::clamp(game, 0.0, 1.0));
      }
      // One outlier a texel: the first of its values past TOLERANCE.
      const bool texel_counted = [&] {
        for (size_t j = i - i % 4; j < i; ++j) {
          const double g = std::bit_cast<float>(reference.words[t][j]);
          const double o = std::bit_cast<float>(test.words[t][j]);
          if (reference.words[t][j] != test.words[t][j] && !(std::abs(o - g) / std::max(1.0, std::abs(g)) <= TOLERANCE)) return true;
        }
        return false;
      }();
      if (error > TOLERANCE && !texel_counted) ++difference.outliers;
      difference.worst = std::max(difference.worst, error);
      difference.clamped = std::max(difference.clamped, clamped);
    }
  }
  return difference;
}

struct Case {
  std::string game, shader, game_path, addon_path;
  Settings settings;
  std::string registers;  // GameConstants' registers
};

static std::vector<Case> ReadManifest(const std::filesystem::path& path) {
  std::vector<Case> cases;
  std::ifstream file(path);
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, '\t')) fields.push_back(field);
    if (fields.size() < 4) continue;
    Case entry{.game = fields[0], .shader = fields[1], .game_path = fields[2], .addon_path = fields[3]};
    if (fields.size() >= 5) {
      std::stringstream settings(fields[4]);
      std::string pair;
      while (std::getline(settings, pair, ',')) {
        const size_t equals = pair.find('=');
        if (equals != std::string::npos) entry.settings.emplace_back(pair.substr(0, equals), std::stof(pair.substr(equals + 1)));
      }
    }
    if (fields.size() >= 6) entry.registers = fields[5];
    cases.push_back(entry);
  }
  return cases;
}

// --dump: the worst texels of `test` against `reference` (target, x, y, channel: the game's, the add-on's).
static void DumpWorst(const char* label, const Output& reference, const Output& test, UINT width) {
  std::vector<std::pair<double, size_t>> errors;
  for (size_t t = 0; t < reference.words.size() && t < test.words.size(); ++t) {
    for (size_t i = 0; i < reference.words[t].size() && i < test.words[t].size(); ++i) {
      const double a = std::bit_cast<float>(reference.words[t][i]);
      const double b = std::bit_cast<float>(test.words[t][i]);
      const double error = (std::isnan(a) != std::isnan(b)) ? INFINITY : std::abs(a - b);
      if (error > 0.0) errors.emplace_back(error, t * 100000000 + i);
    }
  }
  std::sort(errors.rbegin(), errors.rend());
  std::printf("   %s: %zu values differ", label, errors.size());
  if (!reference.words.empty() && reference.words[0].size() >= 4 * (width * 24 + 33)) {
    const size_t at = 4 * (width * 24 + 32);
    std::printf(" (the game's texel 32, 24: %.6g %.6g %.6g %.6g)", std::bit_cast<float>(reference.words[0][at]),
                std::bit_cast<float>(reference.words[0][at + 1]), std::bit_cast<float>(reference.words[0][at + 2]),
                std::bit_cast<float>(reference.words[0][at + 3]));
  }
  std::printf("\n");
  for (size_t k = 0; k < std::min<size_t>(errors.size(), 8); ++k) {
    const size_t t = errors[k].second / 100000000;
    const size_t i = errors[k].second % 100000000;
    const size_t texel = i / 4;
    const auto value = [&](const Output& output, size_t c) { return std::bit_cast<float>(output.words[t][texel * 4 + c]); };
    std::printf("     target %zu x %zu y %zu layer %zu .%c: game (%.6g %.6g %.6g %.6g), test (%.6g %.6g %.6g %.6g)\n", t, texel % width,
                (texel / width) % HEIGHT, texel / (width * HEIGHT), "xyzw"[i % 4], value(reference, 0), value(reference, 1),
                value(reference, 2), value(reference, 3), value(test, 0), value(test, 1), value(test, 2), value(test, 3));
  }
}

static std::string Number(double value) {
  if (!std::isfinite(value)) return "inf";
  if (value == 0.0) return "0";
  char text[32];
  std::snprintf(text, sizeof(text), "%.2g", value);
  return text;
}

int main(int argc, char** argv) {
  REQUIRE(argc >= 2);
  REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)));
  D3D11_FEATURE_DATA_D3D11_OPTIONS3 options3 = {};
  layer_output = SUCCEEDED(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS3, &options3, sizeof(options3)))
                 && options3.VPAndRTArrayIndexFromAnyShaderFeedingRasterizer;
  bool run_mutants = true;
  std::string dump;  // --dump <parts of shader names, comma-separated>: their worst texels
  for (int a = 2; a < argc; ++a) {
    if (std::string_view(argv[a]) == "--no-mutants") run_mutants = false;
    if (std::string_view(argv[a]) == "--dump" && a + 1 < argc) dump = argv[++a];
  }
  int exact = 0, r6 = 0, failed = 0, blind = 0;
  std::printf("%-12s %-34s %-6s %-9s %-9s %-9s %-9s %s\n", "game", "shader", "result", "exact", "full", "control", "mutants", "notes");
  for (const Case& entry : ReadManifest(argv[1])) {
    const Shader game = LoadShader(entry.game_path);
    const Shader addon = LoadShader(entry.addon_path);
    if (!game.reflection || !addon.reflection) {
      std::printf("%-12s %-34s FAIL   (%s missing)\n", entry.game.c_str(), entry.shader.c_str(), game.reflection ? "add-on cso" : "game cso");
      ++failed;
      continue;
    }
    const bool bright_pass = Lower(entry.shader).starts_with("bloom");
    const ComPtr<ID3D11VertexShader> vertex = game.vertex ? ComPtr<ID3D11VertexShader>() : VertexShaderFor(game.reflection.Get());
    const auto run = [&](const std::vector<uint8_t>& code, const Setup& setup) {
      if (game.vertex) return DrawVertex(code, game, setup);
      return vertex ? DrawPixel(code, game.reflection.Get(), vertex.Get(), setup) : Output{};
    };
    const Setup exact_setup = MakeSetup(game, addon, entry.settings, entry.registers, false, bright_pass);
    const Output reference = run(game.code, exact_setup);
    const Output exact_output = run(addon.code, exact_setup);
    const Difference exact_difference = Compare(reference, exact_output);
    bool dumping = false;
    std::stringstream parts(dump);
    for (std::string part; std::getline(parts, part, ',');) dumping = dumping || (!part.empty() && Has(entry.game + " " + entry.shader, part));
    if (dumping) DumpWorst("exact set", reference, exact_output, exact_setup.frame.target_width);
    Difference full_difference = exact_difference;
    if (exact_setup.bloom || bright_pass) {
      const Setup full_setup = MakeSetup(game, addon, entry.settings, entry.registers, true, bright_pass);
      const Output full_reference = run(game.code, full_setup);
      const Output full_output = run(addon.code, full_setup);
      full_difference = Compare(full_reference, full_output);
      if (dumping) DumpWorst("full set", full_reference, full_output, full_setup.frame.target_width);
    }
    // The control and the mutants: the game's bytes changed, against the game's own (the exact set).
    const auto swapped = mutants::SwapOutputSwizzle(game.code);
    const Output control_output = swapped.empty() ? Output{} : run(swapped, exact_setup);
    const Difference control = Compare(reference, control_output);
    if (dumping) DumpWorst("control", reference, control_output, exact_setup.frame.target_width);
    const bool caught = control.valid && !Exact(control);
    std::string mutant_text = "-";
    if (run_mutants) {
      twattila::dxbc::Program program = twattila::dxbc::Read(game.code);
      const auto constants = mutants::FloatConstants(program.tokens);
      int seen = 0;
      for (const size_t token : constants) {
        const uint32_t kept = program.tokens[token];
        program.tokens[token] = std::bit_cast<uint32_t>(std::bit_cast<float>(kept) * 1.02f);
        const Difference mutant = Compare(reference, run(twattila::dxbc::Write(game.code, program), exact_setup));
        if (!Exact(mutant)) ++seen;
        program.tokens[token] = kept;
      }
      mutant_text = std::to_string(seen) + "/" + std::to_string(constants.size());
    }
    const char* result = "FAIL";
    if (!exact_difference.valid || !reference.words.size()) {
      result = "FAIL";
    } else if (Exact(full_difference)) {
      result = (full_difference.worst <= TOLERANCE) ? "EXACT" : "EXACT*";
    } else if (Exact(exact_difference) && (exact_setup.bloom || bright_pass)) {
      result = "R6";
    }
    std::string note = exact_setup.note;
    if (!exact_difference.valid) note += " (no comparable output: the add-on's shader or its signature)";
    if (exact_difference.valid && !Exact(exact_difference) && exact_difference.clamped <= TOLERANCE) note += " (equal within 0-1: apart above 1 only)";
    if (std::string_view(result) == "EXACT*") note += " (" + std::to_string(full_difference.outliers) + " of " + std::to_string(full_difference.texels) + " texels past the tolerance)";
    if (!caught) note += " CONTROL NOT CAUGHT";
    if (std::string_view(result).starts_with("EXACT")) ++exact;
    if (std::string_view(result) == "R6") ++r6;
    if (std::string_view(result) == "FAIL") ++failed;
    if (!caught) ++blind;
    std::printf("%-12s %-34s %-6s %-9s %-9s %-9s %-9s%s\n", entry.game.c_str(), entry.shader.c_str(), result,
                Number(exact_difference.worst).c_str(), Number(full_difference.worst).c_str(), caught ? Number(control.worst).c_str() : (swapped.empty() ? "NONE" : ("MISSED" + Number(control.worst)).c_str()),
                mutant_text.c_str(), note.c_str());
  }
  std::printf("EXACT %d, R6 (bloom smoothing, known) %d, FAIL %d, control missed %d\n", exact, r6, failed, blind);
  return (failed == 0 && blind == 0) ? 0 : 1;
}
