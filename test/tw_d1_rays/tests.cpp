// Divinity: Original Sin EE's helper inputs and ray-traced replacements (upscaling.hpp, ray_tracing.hpp) on WARP: the
// add-on's d1_ shaders compiled by run.ps1 with the build's flags, the game's own sun pass (0xCCCB7253,
// DeferredDirectionalWithSSAO, the census's bytes) reading what they write. The camera, the sun pass's invViewMatrix and
// PerFrame are R1's (tools/nontw/divinity1/r1/run_r1_014005/traces/save_T1_trace.txt, the sun pass's b0 / b12 / b13).
// 1. d1_upscale_inputs' motion: each pixel's point found through the jittered camera (the picture as drawn), moved by
//    last frame's camera, against the shader's within 0.002 px (0.06 px at 3840 wide); a still camera with the jitter on
//    gives none; the depth is the projection's (A + B / z). Controls (must fail): the motion's sign turned (now -
//    before), y not flipped.
// 2. The colour's round trip (d1_upscale_inputs' square root, an identity upscaler, d1_upscale_output's square) gives
//    the scene back within fp16; control (must fail): the square left out.
// 3. d1_rt_guides' world normals against the game's sun pass's own decode (six draws, the sun along +-x, +-y, +-z: its
//    N.L), the albedos byte for byte, the roughness from the Blinn exponent; control (must fail): the view's columns
//    in place of its rows.
// 4. d1_rt_shadow's mask = the shadow filter's form (every channel the visibility) byte for byte, and the game's sun
//    pass gives the same light from it; control (must fail): the visibility in red alone.
// 5. d1_rt_ambient over the SSAO result (half size): each texel the average of the 2 x 2 render pixels it covers, and
//    the game's sun pass gives the same light as from that average; control (must fail): the texel step without the
//    render / target sizes (the top-left quarter stretched).
// 6. d1_rt_inputs: each position projected by the jittered camera lands on its pixel's centre, at the depth's distance;
//    the sky none; the sun texels from PerFrame. Control (must fail): the jitter not taken out.
// 7. d1_rt_sky's sky texels (Path-Traced Ambient) against the game's sun pass lighting a white surface facing each world
//    axis with R1's sky and ground (its sun and specular off), their w, zeros with it off, the other texels untouched.
//    Control (must fail): the opposite axis' texel.
// 8. d1_rt_sky's PerFrame without the ambient, copied into a constant buffer as the add-on binds it: the game's sun pass
//    from it equals, bit for bit, the pass from R1's PerFrame with the sky and ground zeroed (its registers 4-7 byte-equal).
//    Control (must fail): R1's PerFrame as it is (the ambient in).
// 9. d1_rt_bounce_apply: a uniform resolved light L on a wall gives L x the diffuse colour x the strength, against the game's
//    sun pass lighting that G-buffer with an ambient of L (sky = ground); the sky pixels nothing. Control (must fail):
//    gBuffer4's specular colours in gBuffer3's place.
// d1_rays.exe <sun pass cso> <upscale_inputs> <upscale_output> <rt_inputs> <rt_guides> <rt_shadow> <rt_ambient> <full-screen vs>
//   <rt_sky> <rt_bounce_apply>.
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <vector>

using Microsoft::WRL::ComPtr;
using Float4 = std::array<float, 4>;
using Matrix = std::array<std::array<double, 4>, 4>;

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (false)
#define REQUIRE(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

constexpr UINT WIDTH = 128;
constexpr UINT HEIGHT = 72;

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;

// R1's camera (the sun pass's PerView b12: View rows, Projection rows, ViewPos, ViewInfo far 200).
constexpr Matrix VIEW = {{{0.47485608, -0.0, 0.88006353, -434.66394},
                          {-0.6309851, 0.6970971, 0.34046078, 102.41439},
                          {-0.6134897, -0.71697676, 0.33102077, 125.72453},
                          {0.0, 0.0, 0.0, 1.0}}};
constexpr Matrix PROJECTION = {{{1.357995, 0.0, 0.0, 0.0}, {0.0, 2.4142134, 0.0, 0.0}, {0.0, 0.0, 1.0005003, -0.10005005}, {0.0, 0.0, 1.0, 0.0}}};
constexpr std::array<float, 4> VIEW_POSITION = {348.15546f, 18.748796f, 306.04633f, 3.f};
constexpr float FAR_PLANE = 200.f;
// The sun pass's $Globals b0 (shaderProperties, invViewMatrix's rows: the view's rotation transposed, the camera in w).
constexpr std::array<float, 16> SUN_GLOBALS = {1.f, 1.f, 1.f, 1.f, 0.47485602f, -0.630985f, -0.6134897f, 348.15546f,
                                               0.f, 0.69709706f, -0.71697676f, 18.748789f, 0.8800634f, 0.34046075f, 0.33102077f, 306.04636f};
// PerFrame b13's first registers (global_LightPropertyMatrix, column-major: x toward the sun, y sky, z sun, w specular;
// the last register the ground's ambient).
constexpr std::array<float, 16> PER_FRAME = {-0.5f, 0.29765308f, 1.f, 1.f, 0.70710677f, 0.7011688f, 0.9658153f, 1.f,
                                             -0.49999994f, 1.f, 0.45908016f, 1.f, 0.25f, 0.25f, 0.25f, 0.f};
// The whole of R1's PerFrame (128 bytes: the light, the fog's registers, global_Data).
constexpr std::array<float, 32> PER_FRAME_FULL = {-0.5f, 0.29765308f, 1.f, 1.f, 0.70710677f, 0.7011688f, 0.9658153f, 1.f,
                                                  -0.49999994f, 1.f, 0.45908016f, 1.f, 0.25f, 0.25f, 0.25f, 0.f,
                                                  0.8147317f, 0.38890973f, 20.f, 1.f, 2.4831772f, 0.6523694f, 60.f, 10.f,
                                                  2.974176f, 1.f, -15.f, 10.f, 215.61876f, 0.0082251f, 0.48480964f, -0.8746197f};

static Matrix Multiply(const Matrix& a, const Matrix& b) {
  Matrix out = {};
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      for (int k = 0; k < 4; ++k) out[r][c] += a[r][k] * b[k][c];
    }
  }
  return out;
}

static Matrix Inverse(Matrix m) {
  Matrix inverse = {};
  for (int i = 0; i < 4; ++i) inverse[i][i] = 1.0;
  for (int column = 0; column < 4; ++column) {
    int pivot = column;
    for (int row = column + 1; row < 4; ++row) {
      if (std::abs(m[row][column]) > std::abs(m[pivot][column])) pivot = row;
    }
    std::swap(m[column], m[pivot]);
    std::swap(inverse[column], inverse[pivot]);
    const double scale = 1.0 / m[column][column];
    for (int c = 0; c < 4; ++c) {
      m[column][c] *= scale;
      inverse[column][c] *= scale;
    }
    for (int row = 0; row < 4; ++row) {
      if (row == column) continue;
      const double factor = m[row][column];
      for (int c = 0; c < 4; ++c) {
        m[row][c] -= factor * m[column][c];
        inverse[row][c] -= factor * inverse[column][c];
      }
    }
  }
  return inverse;
}

static std::array<double, 4> Transform(const Matrix& m, const std::array<double, 4>& v) {
  std::array<double, 4> out = {};
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) out[r] += m[r][c] * v[c];
  }
  return out;
}

static std::array<float, 16> Rows(const Matrix& m) {
  std::array<float, 16> out = {};
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) out[r * 4 + c] = static_cast<float>(m[r][c]);
  }
  return out;
}

// The picture's camera: rows 0 and 1 moved by the jitter (NDC) times row 3 (upscaling.hpp OnPerView).
static Matrix Jittered(Matrix m, double jitter_x, double jitter_y) {
  for (int c = 0; c < 4; ++c) {
    m[0][c] += jitter_x * m[3][c];
    m[1][c] += jitter_y * m[3][c];
  }
  return m;
}

static uint32_t random_state = 12345u;
static float Random() {
  random_state = random_state * 1664525u + 1013904223u;
  return static_cast<float>(random_state >> 8) / 16777216.f;
}

static float HalfToFloat(uint16_t half) {
  const uint32_t sign = (half >> 15) & 1u;
  const uint32_t exponent = (half >> 10) & 31u;
  const uint32_t mantissa = half & 1023u;
  float value;
  if (exponent == 0) {
    value = std::ldexp(static_cast<float>(mantissa), -24);
  } else if (exponent == 31) {
    value = mantissa != 0 ? NAN : INFINITY;
  } else {
    value = std::ldexp(static_cast<float>(mantissa + 1024), static_cast<int>(exponent) - 25);
  }
  return sign != 0 ? -value : value;
}

static std::vector<char> ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static ComPtr<ID3D11Texture2D> Texture(DXGI_FORMAT format, UINT width, UINT height, UINT bind, const void* data = nullptr, UINT pitch = 0) {
  const D3D11_TEXTURE2D_DESC desc = {width, height, 1, 1, format, {1, 0}, D3D11_USAGE_DEFAULT, bind, 0, 0};
  const D3D11_SUBRESOURCE_DATA init = {data, pitch, 0};
  ComPtr<ID3D11Texture2D> texture;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, data != nullptr ? &init : nullptr, &texture)));
  return texture;
}

static ComPtr<ID3D11ShaderResourceView> View(ID3D11Texture2D* texture) {
  ComPtr<ID3D11ShaderResourceView> view;
  REQUIRE(SUCCEEDED(device->CreateShaderResourceView(texture, nullptr, &view)));
  return view;
}

static ComPtr<ID3D11UnorderedAccessView> Unordered(ID3D11Texture2D* texture) {
  ComPtr<ID3D11UnorderedAccessView> view;
  REQUIRE(SUCCEEDED(device->CreateUnorderedAccessView(texture, nullptr, &view)));
  return view;
}

static ComPtr<ID3D11RenderTargetView> Target(ID3D11Texture2D* texture) {
  ComPtr<ID3D11RenderTargetView> view;
  REQUIRE(SUCCEEDED(device->CreateRenderTargetView(texture, nullptr, &view)));
  return view;
}

template <typename T>
static ComPtr<ID3D11Buffer> Constants(const T& data) {
  const D3D11_BUFFER_DESC desc = {(sizeof(T) + 15) / 16 * 16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
  std::vector<char> padded(desc.ByteWidth);
  std::memcpy(padded.data(), &data, sizeof(T));
  const D3D11_SUBRESOURCE_DATA init = {padded.data(), 0, 0};
  ComPtr<ID3D11Buffer> buffer;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&desc, &init, &buffer)));
  return buffer;
}

// A texture's rows read back as bytes (`texel` bytes a texel).
static std::vector<uint8_t> Read(ID3D11Texture2D* texture, UINT texel) {
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)));
  context->CopyResource(staging.Get(), texture);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  REQUIRE(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
  std::vector<uint8_t> bytes(static_cast<size_t>(desc.Width) * desc.Height * texel);
  for (UINT row = 0; row < desc.Height; ++row) {
    std::memcpy(&bytes[static_cast<size_t>(row) * desc.Width * texel], static_cast<const uint8_t*>(mapped.pData) + row * mapped.RowPitch,
                static_cast<size_t>(desc.Width) * texel);
  }
  context->Unmap(staging.Get(), 0);
  return bytes;
}

static std::vector<Float4> ReadFloat4(ID3D11Texture2D* texture) {
  const auto bytes = Read(texture, 16);
  std::vector<Float4> texels(bytes.size() / 16);
  std::memcpy(texels.data(), bytes.data(), bytes.size());
  return texels;
}

static std::vector<float> ReadBuffer(ID3D11Buffer* buffer) {
  D3D11_BUFFER_DESC desc = {};
  buffer->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.MiscFlags = 0;
  ComPtr<ID3D11Buffer> staging;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&desc, nullptr, &staging)));
  context->CopyResource(staging.Get(), buffer);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  REQUIRE(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
  std::vector<float> values(desc.ByteWidth / 4);
  std::memcpy(values.data(), mapped.pData, desc.ByteWidth);
  context->Unmap(staging.Get(), 0);
  return values;
}

static std::vector<Float4> ReadHalf4(ID3D11Texture2D* texture) {
  const auto bytes = Read(texture, 8);
  std::vector<Float4> texels(bytes.size() / 8);
  for (size_t i = 0; i < texels.size(); ++i) {
    for (int c = 0; c < 4; ++c) {
      uint16_t half = 0;
      std::memcpy(&half, &bytes[i * 8 + c * 2], 2);
      texels[i][c] = HalfToFloat(half);
    }
  }
  return texels;
}

struct Shaders {
  ComPtr<ID3D11PixelShader> sun_pass;
  ComPtr<ID3D11ComputeShader> upscale_inputs;
  ComPtr<ID3D11PixelShader> upscale_output;
  ComPtr<ID3D11ComputeShader> rt_inputs;
  ComPtr<ID3D11ComputeShader> rt_guides;
  ComPtr<ID3D11PixelShader> rt_shadow;
  ComPtr<ID3D11PixelShader> rt_ambient;
  ComPtr<ID3D11ComputeShader> rt_sky;
  ComPtr<ID3D11PixelShader> rt_bounce_apply;
  ComPtr<ID3D11VertexShader> full_screen;  // the add-on's (light_bounce_vs: SV_Position only)
  ComPtr<ID3D11VertexShader> sun_vertex;   // the sun pass's interface: SV_Position, TexCoord0 (uv), TexCoord1 (the view ray)
  ComPtr<ID3D11SamplerState> linear;
};
static Shaders shaders;

static void Dispatch(ID3D11ComputeShader* shader, std::initializer_list<ID3D11ShaderResourceView*> inputs,
                     std::initializer_list<ID3D11UnorderedAccessView*> outputs, std::initializer_list<ID3D11Buffer*> constants) {
  context->ClearState();
  context->CSSetShader(shader, nullptr, 0);
  context->CSSetShaderResources(0, static_cast<UINT>(inputs.size()), inputs.begin());
  context->CSSetUnorderedAccessViews(0, static_cast<UINT>(outputs.size()), outputs.begin(), nullptr);
  context->CSSetConstantBuffers(0, static_cast<UINT>(constants.size()), constants.begin());
  context->Dispatch((WIDTH + 7) / 8, (HEIGHT + 7) / 8, 1);
}

static void Draw(ID3D11VertexShader* vertex, ID3D11PixelShader* pixel, ID3D11RenderTargetView* target, UINT width, UINT height,
                 std::initializer_list<ID3D11ShaderResourceView*> inputs, const std::vector<std::pair<UINT, ID3D11Buffer*>>& constants) {
  context->ClearState();
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(vertex, nullptr, 0);
  context->PSSetShader(pixel, nullptr, 0);
  context->PSSetShaderResources(0, static_cast<UINT>(inputs.size()), inputs.begin());
  for (const auto& [slot, buffer] : constants) context->PSSetConstantBuffers(slot, 1, &buffer);
  context->PSSetSamplers(0, 1, shaders.linear.GetAddressOf());
  const D3D11_VIEWPORT viewport = {0.f, 0.f, static_cast<float>(width), static_cast<float>(height), 0.f, 1.f};
  context->RSSetViewports(1, &viewport);
  context->OMSetRenderTargets(1, &target, nullptr);
  context->Draw(3, 0);
}

// The game's G-buffer for its sun pass (t0-t4), and its b0 / b12 / b13.
struct SunInputs {
  ComPtr<ID3D11ShaderResourceView> emissive, depth, normals, diffuse, specular;
  ComPtr<ID3D11Buffer> globals, per_view, per_frame;
};

static ComPtr<ID3D11Buffer> PerView() {
  std::array<float, 56> per_view = {};
  const auto view = Rows(VIEW);
  const auto projection = Rows(PROJECTION);
  const auto view_projection = Rows(Multiply(PROJECTION, VIEW));
  std::copy(view.begin(), view.end(), per_view.begin());
  std::copy(projection.begin(), projection.end(), per_view.begin() + 16);
  std::copy(view_projection.begin(), view_projection.end(), per_view.begin() + 32);
  std::copy(VIEW_POSITION.begin(), VIEW_POSITION.end(), per_view.begin() + 48);
  per_view[52] = FAR_PLANE;
  per_view[53] = 0.1f;
  per_view[54] = static_cast<float>(WIDTH);
  per_view[55] = static_cast<float>(HEIGHT);
  return Constants(per_view);
}

// One draw of the game's sun pass into RGBA32F: its light from the G-buffer, `occlusion` (t5) and `shadow` (t6).
static std::vector<Float4> SunPass(const SunInputs& inputs, ID3D11ShaderResourceView* occlusion, ID3D11ShaderResourceView* shadow) {
  const auto target = Texture(DXGI_FORMAT_R32G32B32A32_FLOAT, WIDTH, HEIGHT, D3D11_BIND_RENDER_TARGET);
  Draw(shaders.sun_vertex.Get(), shaders.sun_pass.Get(), Target(target.Get()).Get(), WIDTH, HEIGHT,
       {inputs.emissive.Get(), inputs.depth.Get(), inputs.normals.Get(), inputs.diffuse.Get(), inputs.specular.Get(), occlusion, shadow},
       {{0, inputs.globals.Get()}, {12, inputs.per_view.Get()}, {13, inputs.per_frame.Get()}});
  return ReadFloat4(target.Get());
}

static float Largest(const std::vector<Float4>& a, const std::vector<Float4>& b) {
  float worst = 0.f;
  for (size_t i = 0; i < a.size(); ++i) {
    for (int c = 0; c < 3; ++c) worst = std::max(worst, std::abs(a[i][c] - b[i][c]));
  }
  return worst;
}

// Random G-buffer contents (R32F linear depth, RG32F encoded normals, RGBA8 colours), a few sky pixels.
struct GBuffer {
  std::vector<float> depth;
  std::vector<std::array<float, 2>> normals;
  std::vector<uint8_t> diffuse, specular;
};

static GBuffer RandomGBuffer() {
  GBuffer g;
  for (UINT i = 0; i < WIDTH * HEIGHT; ++i) {
    g.depth.push_back((i % 23 == 0) ? 1.f : 0.01f + 0.6f * Random());
    g.normals.push_back({0.2f + 0.6f * Random(), 0.2f + 0.6f * Random()});
    for (int c = 0; c < 4; ++c) {
      g.diffuse.push_back(static_cast<uint8_t>(Random() * 255.f));
      g.specular.push_back(static_cast<uint8_t>(Random() * 255.f));
    }
  }
  return g;
}

static SunInputs MakeSunInputs(const GBuffer& g, const std::array<float, 16>& per_frame, bool lit_white) {
  SunInputs inputs;
  std::vector<Float4> emissive(WIDTH * HEIGHT, Float4{});
  if (!lit_white) {
    for (auto& texel : emissive) texel = {0.02f * Random(), 0.02f * Random(), 0.02f * Random(), 0.f};
  }
  inputs.emissive = View(Texture(DXGI_FORMAT_R32G32B32A32_FLOAT, WIDTH, HEIGHT, D3D11_BIND_SHADER_RESOURCE, emissive.data(), WIDTH * 16).Get());
  inputs.depth = View(Texture(DXGI_FORMAT_R32_FLOAT, WIDTH, HEIGHT, D3D11_BIND_SHADER_RESOURCE, g.depth.data(), WIDTH * 4).Get());
  inputs.normals = View(Texture(DXGI_FORMAT_R32G32_FLOAT, WIDTH, HEIGHT, D3D11_BIND_SHADER_RESOURCE, g.normals.data(), WIDTH * 8).Get());
  // White diffuse, no specular: the pass's output is its N.L (section 3).
  const std::vector<uint8_t> white(WIDTH * HEIGHT * 4, 255);
  const std::vector<uint8_t> black(WIDTH * HEIGHT * 4, 0);
  inputs.diffuse = View(Texture(DXGI_FORMAT_R8G8B8A8_UNORM, WIDTH, HEIGHT, D3D11_BIND_SHADER_RESOURCE,
                                lit_white ? white.data() : g.diffuse.data(), WIDTH * 4).Get());
  inputs.specular = View(Texture(DXGI_FORMAT_R8G8B8A8_UNORM, WIDTH, HEIGHT, D3D11_BIND_SHADER_RESOURCE,
                                 lit_white ? black.data() : g.specular.data(), WIDTH * 4).Get());
  inputs.globals = Constants(SUN_GLOBALS);
  inputs.per_view = PerView();
  inputs.per_frame = Constants(per_frame);
  return inputs;
}

int main(int argc, char** argv) {
  // Unbuffered: a section's lines reach the log even if a later one stops the program.
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  REQUIRE(argc >= 11);
  REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)));
  const auto pixel = [](const char* path) {
    const auto code = ReadFile(path);
    REQUIRE(!code.empty());
    ComPtr<ID3D11PixelShader> shader;
    REQUIRE(SUCCEEDED(device->CreatePixelShader(code.data(), code.size(), nullptr, &shader)));
    return shader;
  };
  const auto compute = [](const char* path) {
    const auto code = ReadFile(path);
    REQUIRE(!code.empty());
    ComPtr<ID3D11ComputeShader> shader;
    REQUIRE(SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, &shader)));
    return shader;
  };
  shaders.sun_pass = pixel(argv[1]);
  shaders.upscale_inputs = compute(argv[2]);
  shaders.upscale_output = pixel(argv[3]);
  shaders.rt_inputs = compute(argv[4]);
  shaders.rt_guides = compute(argv[5]);
  shaders.rt_shadow = pixel(argv[6]);
  shaders.rt_ambient = pixel(argv[7]);
  {
    const auto code = ReadFile(argv[8]);
    REQUIRE(!code.empty());
    REQUIRE(SUCCEEDED(device->CreateVertexShader(code.data(), code.size(), nullptr, &shaders.full_screen)));
  }
  shaders.rt_sky = compute(argv[9]);
  shaders.rt_bounce_apply = pixel(argv[10]);
  constexpr char SUN_VERTEX[] =
      "void main(uint id : SV_VertexID, out float4 position : SV_Position, out float2 uv : TexCoord0, out float3 ray : TexCoord1) {"
      "  uv = float2((id << 1) & 2, id & 2); position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);"
      "  ray = float3(position.xy * 0.5, 1); }";
  ComPtr<ID3DBlob> vertex_code;
  REQUIRE(SUCCEEDED(D3DCompile(SUN_VERTEX, sizeof(SUN_VERTEX) - 1, nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vertex_code, nullptr)));
  REQUIRE(SUCCEEDED(device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, &shaders.sun_vertex)));
  const D3D11_SAMPLER_DESC sampler = {D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP,
                                      D3D11_TEXTURE_ADDRESS_CLAMP, 0.f, 1, D3D11_COMPARISON_NEVER, {0.f, 0.f, 0.f, 0.f}, 0.f, D3D11_FLOAT32_MAX};
  REQUIRE(SUCCEEDED(device->CreateSamplerState(&sampler, &shaders.linear)));

  const Matrix view_projection = Multiply(PROJECTION, VIEW);
  const Matrix inverse_view = Inverse(VIEW);
  // upscaling.hpp's motion inputs: the inverse view around the camera (no translation), a view-projection moved to the
  // camera (doubles).
  Matrix inverse_rotation = inverse_view;
  for (int r = 0; r < 3; ++r) inverse_rotation[r][3] = 0.0;
  // The view-projection as the game uploads it (floats), moved in doubles.
  const auto around_camera = [&](const Matrix& view_projection_of) {
    Matrix moved = {};
    for (int r = 0; r < 4; ++r) {
      for (int c = 0; c < 4; ++c) moved[r][c] = static_cast<float>(view_projection_of[r][c]);
    }
    for (int r = 0; r < 4; ++r) {
      for (int c = 0; c < 3; ++c) moved[r][3] += moved[r][c] * static_cast<float>(inverse_view[c][3]);
    }
    return Rows(moved);
  };
  // The lens without the jitter: P00, P11, P02, P12.
  const std::array<float, 4> lens = {static_cast<float>(PROJECTION[0][0]), static_cast<float>(PROJECTION[1][1]), static_cast<float>(PROJECTION[0][2]),
                                     static_cast<float>(PROJECTION[1][2])};
  // Last frame's camera: 2 degrees of yaw and a step (0.4 right, 0.3 up, 0.5 back) in view space.
  const double yaw = 2.0 * std::numbers::pi / 180.0;
  const Matrix step = {{{std::cos(yaw), 0.0, std::sin(yaw), 0.4}, {0.0, 1.0, 0.0, 0.3}, {-std::sin(yaw), 0.0, std::cos(yaw), -0.5}, {0.0, 0.0, 0.0, 1.0}}};
  const Matrix previous_view_projection = Multiply(PROJECTION, Multiply(step, VIEW));
  // The jitter: (0.3125, -0.2083) pixels, in NDC (upscaling.hpp MainCamera).
  const double jitter_x = 2.0 * 0.3125 / WIDTH;
  const double jitter_y = 2.0 * -0.2083 / HEIGHT;
  const Matrix jittered = Jittered(view_projection, jitter_x, jitter_y);
  const Matrix inverse_jittered = Inverse(jittered);
  const double depth_a = PROJECTION[2][2];
  const double depth_b = PROJECTION[2][3];
  const auto centre_ndc = [](UINT x, UINT y) {
    return std::array<double, 2>{(x + 0.5) / WIDTH * 2.0 - 1.0, 1.0 - (y + 0.5) / HEIGHT * 2.0};
  };
  // The pixel's point in the jittered picture at view z (its camera's own inverse).
  const auto picture_point = [&](UINT x, UINT y, double view_z) {
    const auto ndc = centre_ndc(x, y);
    auto world = Transform(inverse_jittered, {ndc[0] * view_z, ndc[1] * view_z, depth_a * view_z + depth_b, view_z});
    for (auto& v : world) v /= world[3];
    return world;
  };

  const GBuffer g = RandomGBuffer();
  const auto depth = Texture(DXGI_FORMAT_R32_FLOAT, WIDTH, HEIGHT, D3D11_BIND_SHADER_RESOURCE, g.depth.data(), WIDTH * 4);
  const auto depth_view = View(depth.Get());

  // 1. Motion and depth.
  {
    std::vector<Float4> scene(WIDTH * HEIGHT);
    for (auto& texel : scene) texel = {8.f * Random() * Random(), 4.f * Random(), Random(), 1.f};
    const auto scene_texture = Texture(DXGI_FORMAT_R32G32B32A32_FLOAT, WIDTH, HEIGHT, D3D11_BIND_SHADER_RESOURCE, scene.data(), WIDTH * 16);
    struct InputConstants {
      std::array<float, 16> inverse_view, previous_view_projection;
      std::array<float, 4> size, projection, jitter, lens;
    };
    const auto run = [&](const Matrix& previous, float reset) {
      const auto color = Texture(DXGI_FORMAT_R16G16B16A16_FLOAT, WIDTH, HEIGHT, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE);
      const auto device_depth = Texture(DXGI_FORMAT_R32_FLOAT, WIDTH, HEIGHT, D3D11_BIND_UNORDERED_ACCESS);
      const auto motion = Texture(DXGI_FORMAT_R32G32_FLOAT, WIDTH, HEIGHT, D3D11_BIND_UNORDERED_ACCESS);
      const InputConstants constants = {
          .inverse_view = Rows(inverse_rotation),
          .previous_view_projection = around_camera(previous),
          .size = {static_cast<float>(WIDTH), static_cast<float>(HEIGHT), 1.f / WIDTH, 1.f / HEIGHT},
          .projection = {static_cast<float>(depth_a), static_cast<float>(depth_b), FAR_PLANE, 0.f},
          .jitter = {static_cast<float>(jitter_x), static_cast<float>(jitter_y), reset, 0.f},
          .lens = lens,
      };
      Dispatch(shaders.upscale_inputs.Get(), {View(scene_texture.Get()).Get(), depth_view.Get()},
               {Unordered(color.Get()).Get(), Unordered(device_depth.Get()).Get(), Unordered(motion.Get()).Get()}, {Constants(constants).Get()});
      const auto motion_bytes = Read(motion.Get(), 8);
      const auto depth_bytes = Read(device_depth.Get(), 4);
      std::vector<std::array<float, 2>> motions(WIDTH * HEIGHT);
      std::vector<float> depths(WIDTH * HEIGHT);
      std::memcpy(motions.data(), motion_bytes.data(), motion_bytes.size());
      std::memcpy(depths.data(), depth_bytes.data(), depth_bytes.size());
      return std::pair{motions, depths};
    };
    const auto [motions, depths] = run(previous_view_projection, 0.f);
    double worst = 0.0, sign_turned = 0.0, y_unflipped = 0.0, depth_worst = 0.0, depth_control = 0.0, largest_motion = 0.0;
    for (UINT y = 0; y < HEIGHT; ++y) {
      for (UINT x = 0; x < WIDTH; ++x) {
        const size_t i = y * WIDTH + x;
        const double view_z = std::max(g.depth[i], 1e-6f) * FAR_PLANE;
        const auto world = picture_point(x, y, view_z);
        const auto before = Transform(previous_view_projection, world);
        const auto ndc = centre_ndc(x, y);
        // Where the point sits without the jitter now, and last frame: NVIDIA's motion (before - now), pixels, y down.
        const double mx = (before[0] / before[3] - (ndc[0] - jitter_x)) * 0.5 * WIDTH;
        const double my = (before[1] / before[3] - (ndc[1] - jitter_y)) * -0.5 * HEIGHT;
        largest_motion = std::max(largest_motion, std::hypot(mx, my));
        worst = std::max(worst, std::hypot(motions[i][0] - mx, motions[i][1] - my));
        sign_turned = std::max(sign_turned, std::hypot(motions[i][0] + mx, motions[i][1] + my));
        y_unflipped = std::max(y_unflipped, std::hypot(motions[i][0] - mx, motions[i][1] + my));
        const double expected_depth = std::clamp(depth_a + depth_b / view_z, 0.0, 1.0);
        depth_worst = std::max(depth_worst, std::abs(depths[i] - expected_depth));
        depth_control = std::max(depth_control, static_cast<double>(std::abs(depths[i] - g.depth[i])));
      }
    }
    const auto [still, still_depths] = run(view_projection, 0.f);
    const auto [reset, reset_depths] = run(previous_view_projection, 1.f);
    double still_worst = 0.0, reset_worst = 0.0;
    for (size_t i = 0; i < still.size(); ++i) {
      still_worst = std::max(still_worst, static_cast<double>(std::hypot(still[i][0], still[i][1])));
      reset_worst = std::max(reset_worst, static_cast<double>(std::hypot(reset[i][0], reset[i][1])));
    }
    std::printf("1. motion against the jittered picture's points moved by last frame's camera: largest error %.4g px (motion up to %.2f px)\n",
                worst, largest_motion);
    std::printf("   still camera with the jitter on: largest motion %.3g px; reset: %.3g px; depth A + B / z: largest error %.3g\n",
                still_worst, reset_worst, depth_worst);
    std::printf("   controls: sign turned %.3g px, y not flipped %.3g px, the linear depth as device depth %.3g\n", sign_turned, y_unflipped,
                depth_control);
    // 0.002 px here is 0.06 px at 3840 wide (an error grows with the width).
    CHECK(worst <= 0.002);
    CHECK(still_worst <= 0.002);
    CHECK(reset_worst == 0.0);
    CHECK(depth_worst <= 1e-6);
    CHECK(largest_motion > 1.0);
    CHECK(sign_turned > 0.5 && y_unflipped > 0.5 && depth_control > 0.1);
  }

  // 2. The colour's round trip through the square root.
  {
    std::vector<Float4> scene(WIDTH * HEIGHT);
    for (size_t i = 0; i < scene.size(); ++i) {
      const float scale = std::exp2(-12.f + 18.f * Random());
      scene[i] = {scale * Random(), scale * Random(), scale * Random(), 1.f};
    }
    scene[0] = {0.f, 0.f, 0.f, 1.f};
    const auto scene_texture = Texture(DXGI_FORMAT_R32G32B32A32_FLOAT, WIDTH, HEIGHT, D3D11_BIND_SHADER_RESOURCE, scene.data(), WIDTH * 16);
    const auto round_trip = [&](float input_root, float output_root) {
      const auto color = Texture(DXGI_FORMAT_R16G16B16A16_FLOAT, WIDTH, HEIGHT, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE);
      const auto device_depth = Texture(DXGI_FORMAT_R32_FLOAT, WIDTH, HEIGHT, D3D11_BIND_UNORDERED_ACCESS);
      const auto motion = Texture(DXGI_FORMAT_R16G16_FLOAT, WIDTH, HEIGHT, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE);
      struct InputConstants {
        std::array<float, 16> inverse_view, previous_view_projection;
        std::array<float, 4> size, projection, jitter, lens;
      };
      const InputConstants constants = {
          .inverse_view = Rows(inverse_rotation),
          .previous_view_projection = around_camera(view_projection),
          .size = {static_cast<float>(WIDTH), static_cast<float>(HEIGHT), 1.f / WIDTH, 1.f / HEIGHT},
          .projection = {static_cast<float>(depth_a), static_cast<float>(depth_b), FAR_PLANE, input_root},
          .jitter = {0.f, 0.f, 0.f, 0.f},
          .lens = lens,
      };
      Dispatch(shaders.upscale_inputs.Get(), {View(scene_texture.Get()).Get(), depth_view.Get()},
               {Unordered(color.Get()).Get(), Unordered(device_depth.Get()).Get(), Unordered(motion.Get()).Get()}, {Constants(constants).Get()});
      // An identity upscaler: its output is its colour input.
      const auto target = Texture(DXGI_FORMAT_R32G32B32A32_FLOAT, WIDTH, HEIGHT, D3D11_BIND_RENDER_TARGET);
      const std::array<float, 4> output_constants = {output_root, 0.f, 0.f, 0.f};
      const auto color_view = View(color.Get());
      Draw(shaders.full_screen.Get(), shaders.upscale_output.Get(), Target(target.Get()).Get(), WIDTH, HEIGHT,
           {color_view.Get(), View(motion.Get()).Get(), color_view.Get()}, {{0, Constants(output_constants).Get()}});
      const auto out = ReadFloat4(target.Get());
      double worst = 0.0;
      for (size_t i = 0; i < out.size(); ++i) {
        for (int c = 0; c < 3; ++c) worst = std::max(worst, static_cast<double>(std::abs(out[i][c] - scene[i][c]) / std::max(scene[i][c], 1e-4f)));
        if (out[i][3] != 1.f) worst = 1e9;
      }
      return worst;
    };
    const double rooted = round_trip(1.f, 1.f);
    const double plain = round_trip(0.f, 0.f);
    const double control = round_trip(1.f, 0.f);
    std::printf("2. round trip (relative): square root fed and squared back %.3g, plain %.3g; control (not squared back) %.3g\n", rooted, plain,
                control);
    CHECK(rooted <= 2e-3 && plain <= 1e-3);
    CHECK(control > 0.1);
  }

  // 3. The guides against the game's sun pass's decode.
  {
    struct GuideConstants {
      std::array<float, 12> view;
      std::array<float, 4> size;
    };
    const auto guides = [&](bool rows) {
      GuideConstants constants = {.size = {static_cast<float>(WIDTH), static_cast<float>(HEIGHT), 0.f, 0.f}};
      for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) constants.view[r * 4 + c] = static_cast<float>(rows ? VIEW[r][c] : VIEW[c][r]);
      }
      const auto normals = Texture(DXGI_FORMAT_R32G32_FLOAT, WIDTH, HEIGHT, D3D11_BIND_SHADER_RESOURCE, g.normals.data(), WIDTH * 8);
      const auto diffuse = Texture(DXGI_FORMAT_R8G8B8A8_UNORM, WIDTH, HEIGHT, D3D11_BIND_SHADER_RESOURCE, g.diffuse.data(), WIDTH * 4);
      const auto specular = Texture(DXGI_FORMAT_R8G8B8A8_UNORM, WIDTH, HEIGHT, D3D11_BIND_SHADER_RESOURCE, g.specular.data(), WIDTH * 4);
      const auto diffuse_albedo = Texture(DXGI_FORMAT_R8G8B8A8_UNORM, WIDTH, HEIGHT, D3D11_BIND_UNORDERED_ACCESS);
      const auto specular_albedo = Texture(DXGI_FORMAT_R8G8B8A8_UNORM, WIDTH, HEIGHT, D3D11_BIND_UNORDERED_ACCESS);
      const auto world_normals = Texture(DXGI_FORMAT_R16G16B16A16_FLOAT, WIDTH, HEIGHT, D3D11_BIND_UNORDERED_ACCESS);
      const auto roughness = Texture(DXGI_FORMAT_R8_UNORM, WIDTH, HEIGHT, D3D11_BIND_UNORDERED_ACCESS);
      Dispatch(shaders.rt_guides.Get(), {depth_view.Get(), View(normals.Get()).Get(), View(diffuse.Get()).Get(), View(specular.Get()).Get()},
               {Unordered(diffuse_albedo.Get()).Get(), Unordered(specular_albedo.Get()).Get(), Unordered(world_normals.Get()).Get(),
                Unordered(roughness.Get()).Get()},
               {Constants(constants).Get()});
      return std::tuple{ReadHalf4(world_normals.Get()), Read(diffuse_albedo.Get(), 4), Read(specular_albedo.Get(), 4), Read(roughness.Get(), 1)};
    };
    const auto [normals, diffuse_albedo, specular_albedo, roughness] = guides(true);
    const auto control_normals = std::get<0>(guides(false));
    // The game's N.L for the sun along each axis, both ways: white diffuse, no ambient or specular, nothing shadowed.
    const std::vector<uint8_t> ones(WIDTH * HEIGHT * 4, 255);
    const auto shadow = View(Texture(DXGI_FORMAT_R8G8B8A8_UNORM, WIDTH, HEIGHT, D3D11_BIND_SHADER_RESOURCE, ones.data(), WIDTH * 4).Get());
    const std::vector<uint16_t> half_ones((WIDTH / 2) * (HEIGHT / 2) * 4, 0x3C00);
    const auto occlusion = View(Texture(DXGI_FORMAT_R16G16B16A16_FLOAT, WIDTH / 2, HEIGHT / 2, D3D11_BIND_SHADER_RESOURCE, half_ones.data(), WIDTH * 4).Get());
    std::array<std::vector<Float4>, 6> lit;
    for (int axis = 0; axis < 6; ++axis) {
      std::array<float, 16> per_frame = {};
      for (int r = 0; r < 3; ++r) per_frame[r * 4 + 2] = 1.f;  // the sun's light (z), no sky (y) or specular (w)
      per_frame[(axis / 2) * 4] = (axis % 2 == 0) ? 1.f : -1.f;
      lit[axis] = SunPass(MakeSunInputs(g, per_frame, true), occlusion.Get(), shadow.Get());
    }
    double worst = 0.0, control = 0.0;
    size_t sky_wrong = 0;
    for (size_t i = 0; i < normals.size(); ++i) {
      if (g.depth[i] >= 0.999f) {
        sky_wrong += (normals[i][0] != 0.f || normals[i][1] != 0.f || normals[i][2] != 0.f) ? 1 : 0;
        continue;
      }
      for (int c = 0; c < 3; ++c) {
        const double game = lit[c * 2][i][0] - lit[c * 2 + 1][i][0];
        worst = std::max(worst, std::abs(normals[i][c] - game));
        control = std::max(control, std::abs(control_normals[i][c] - game));
      }
    }
    size_t albedo_wrong = 0;
    size_t roughness_worst = 0;
    for (size_t i = 0; i < g.depth.size(); ++i) {
      if (g.depth[i] >= 0.999f) continue;
      for (int c = 0; c < 3; ++c) {
        albedo_wrong += (diffuse_albedo[i * 4 + c] != g.diffuse[i * 4 + c] || specular_albedo[i * 4 + c] != g.specular[i * 4 + c]) ? 1 : 0;
      }
      const double expected = std::sqrt(std::sqrt(2.0 / (g.specular[i * 4 + 3] + 2.0))) * 255.0;
      roughness_worst = std::max(roughness_worst, static_cast<size_t>(std::abs(roughness[i] - expected) + 0.5));
    }
    std::printf("3. guides: world normals against the game's sun pass's N.L, largest difference %.4g (control, the view's columns: %.3g);\n"
                "   albedos not byte-equal %zu, roughness largest error %zu / 255, sky texels not zero %zu\n",
                worst, control, albedo_wrong, roughness_worst, sky_wrong);
    CHECK(worst <= 2e-3);
    CHECK(control > 0.05);
    CHECK(albedo_wrong == 0 && roughness_worst <= 1 && sky_wrong == 0);
  }

  // Sections 4 and 5: the game's sun pass at R1's light over a random G-buffer.
  const SunInputs lit_inputs = MakeSunInputs(g, PER_FRAME, false);
  std::vector<uint8_t> visibility(WIDTH * HEIGHT);
  for (auto& v : visibility) v = static_cast<uint8_t>(Random() * 256.f);
  visibility[0] = 0;
  visibility[1] = 255;
  const auto visibility_view = View(Texture(DXGI_FORMAT_R8_UNORM, WIDTH, HEIGHT, D3D11_BIND_SHADER_RESOURCE, visibility.data(), WIDTH).Get());
  const std::vector<uint16_t> half_ones((WIDTH / 2) * (HEIGHT / 2) * 4, 0x3C00);
  const auto no_occlusion = View(Texture(DXGI_FORMAT_R16G16B16A16_FLOAT, WIDTH / 2, HEIGHT / 2, D3D11_BIND_SHADER_RESOURCE, half_ones.data(), WIDTH * 4).Get());

  // 4. The shadow mask.
  {
    const auto mask = Texture(DXGI_FORMAT_R8G8B8A8_UNORM, WIDTH, HEIGHT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    Draw(shaders.full_screen.Get(), shaders.rt_shadow.Get(), Target(mask.Get()).Get(), WIDTH, HEIGHT, {visibility_view.Get()}, {});
    const auto mask_bytes = Read(mask.Get(), 4);
    std::vector<uint8_t> filter_form(WIDTH * HEIGHT * 4), red_alone(WIDTH * HEIGHT * 4, 0);
    for (size_t i = 0; i < visibility.size(); ++i) {
      for (int c = 0; c < 4; ++c) filter_form[i * 4 + c] = visibility[i];
      red_alone[i * 4] = visibility[i];
      red_alone[i * 4 + 3] = visibility[i];
    }
    size_t unequal = 0;
    for (size_t i = 0; i < mask_bytes.size(); ++i) unequal += (mask_bytes[i] != filter_form[i]) ? 1 : 0;
    const auto make = [](const std::vector<uint8_t>& bytes) {
      return View(Texture(DXGI_FORMAT_R8G8B8A8_UNORM, WIDTH, HEIGHT, D3D11_BIND_SHADER_RESOURCE, bytes.data(), WIDTH * 4).Get());
    };
    const std::vector<uint8_t> ones(WIDTH * HEIGHT * 4, 255);
    const auto ours = SunPass(lit_inputs, no_occlusion.Get(), View(mask.Get()).Get());
    const auto filter = SunPass(lit_inputs, no_occlusion.Get(), make(filter_form).Get());
    const auto red = SunPass(lit_inputs, no_occlusion.Get(), make(red_alone).Get());
    const auto unshadowed = SunPass(lit_inputs, no_occlusion.Get(), make(ones).Get());
    std::printf("4. shadow mask: %zu of %zu bytes unlike the filter's form; the game's sun pass from it against from that form: largest\n"
                "   difference %.3g; controls: the visibility in red alone %.3g, no shadow %.3g\n",
                unequal, mask_bytes.size(), Largest(ours, filter), Largest(red, filter), Largest(unshadowed, filter));
    CHECK(unequal == 0);
    CHECK(Largest(ours, filter) == 0.f);
    CHECK(Largest(red, filter) > 1e-2f && Largest(unshadowed, filter) > 1e-2f);
  }

  // 5. The ambient occlusion over the SSAO result (half size).
  {
    const auto ambient = [&](float step_x, float step_y) {
      const auto target = Texture(DXGI_FORMAT_R16G16B16A16_FLOAT, WIDTH / 2, HEIGHT / 2, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
      const std::array<float, 4> texel = {step_x, step_y, 0.f, 0.f};
      Draw(shaders.full_screen.Get(), shaders.rt_ambient.Get(), Target(target.Get()).Get(), WIDTH / 2, HEIGHT / 2, {visibility_view.Get()},
           {{0, Constants(texel).Get()}});
      return target;
    };
    // ray_tracing.hpp: render / target size / the ambient texture's size (the render's here).
    const auto ours = ambient(static_cast<float>(WIDTH) / (WIDTH / 2) / WIDTH, static_cast<float>(HEIGHT) / (HEIGHT / 2) / HEIGHT);
    const auto stretched = ambient(1.f / WIDTH, 1.f / HEIGHT);
    const auto ours_texels = ReadHalf4(ours.Get());
    std::vector<Float4> average((WIDTH / 2) * (HEIGHT / 2));
    double worst = 0.0;
    for (UINT y = 0; y < HEIGHT / 2; ++y) {
      for (UINT x = 0; x < WIDTH / 2; ++x) {
        const float a = (visibility[(2 * y) * WIDTH + 2 * x] + visibility[(2 * y) * WIDTH + 2 * x + 1] + visibility[(2 * y + 1) * WIDTH + 2 * x]
                         + visibility[(2 * y + 1) * WIDTH + 2 * x + 1])
                        / (4.f * 255.f);
        average[y * (WIDTH / 2) + x] = {a, a, a, a};
        for (int c = 0; c < 4; ++c) worst = std::max(worst, static_cast<double>(std::abs(ours_texels[y * (WIDTH / 2) + x][c] - a)));
      }
    }
    const auto average_texture = Texture(DXGI_FORMAT_R32G32B32A32_FLOAT, WIDTH / 2, HEIGHT / 2, D3D11_BIND_SHADER_RESOURCE, average.data(), (WIDTH / 2) * 16);
    const std::vector<uint8_t> ones(WIDTH * HEIGHT * 4, 255);
    const auto unshadowed = View(Texture(DXGI_FORMAT_R8G8B8A8_UNORM, WIDTH, HEIGHT, D3D11_BIND_SHADER_RESOURCE, ones.data(), WIDTH * 4).Get());
    const auto from_ours = SunPass(lit_inputs, View(ours.Get()).Get(), unshadowed.Get());
    const auto from_average = SunPass(lit_inputs, View(average_texture.Get()).Get(), unshadowed.Get());
    const auto from_stretched = SunPass(lit_inputs, View(stretched.Get()).Get(), unshadowed.Get());
    std::printf("5. ambient occlusion: texels against the 2 x 2 averages, largest difference %.3g; the game's sun pass from ours against from\n"
                "   the averages: %.3g; control (the texel step without the sizes) %.3g\n",
                worst, Largest(from_ours, from_average), Largest(from_stretched, from_average));
    CHECK(worst <= 1e-3);
    CHECK(Largest(from_ours, from_average) <= 2e-3f);
    CHECK(Largest(from_stretched, from_average) > 2e-2f);
  }

  // 6. The rays' positions and sun.
  {
    struct RtConstants {
      std::array<float, 16> inverse_view;
      std::array<float, 4> size, projection, jitter, camera, lens;
    };
    const auto positions_of = [&](float jitter_scale) {
      const RtConstants constants = {
          .inverse_view = Rows(inverse_view),
          .size = {static_cast<float>(WIDTH), static_cast<float>(HEIGHT), 1.f / WIDTH, 1.f / HEIGHT},
          .projection = {static_cast<float>(depth_a), static_cast<float>(depth_b), FAR_PLANE, 0.f},
          .jitter = {static_cast<float>(jitter_x * jitter_scale), static_cast<float>(jitter_y * jitter_scale), 0.f, 0.f},
          .camera = VIEW_POSITION,
          .lens = lens,
      };
      const auto positions = Texture(DXGI_FORMAT_R32G32B32A32_FLOAT, WIDTH, HEIGHT, D3D11_BIND_UNORDERED_ACCESS);
      const auto sun = Texture(DXGI_FORMAT_R32G32B32A32_FLOAT, 16, 1, D3D11_BIND_UNORDERED_ACCESS);
      Dispatch(shaders.rt_inputs.Get(), {depth_view.Get()}, {Unordered(positions.Get()).Get(), Unordered(sun.Get()).Get()},
               {Constants(constants).Get(), Constants(PER_FRAME).Get()});
      return std::pair{ReadFloat4(positions.Get()), ReadFloat4(sun.Get())};
    };
    const auto reprojection = [&](const std::vector<Float4>& positions, double* distance_worst, size_t* sky_wrong) {
      double worst = 0.0;
      for (UINT y = 0; y < HEIGHT; ++y) {
        for (UINT x = 0; x < WIDTH; ++x) {
          const size_t i = y * WIDTH + x;
          const Float4& p = positions[i];
          if (g.depth[i] >= 0.999f) {
            *sky_wrong += (p[0] != 0.f || p[1] != 0.f || p[2] != 0.f || p[3] != 0.f) ? 1 : 0;
            continue;
          }
          const auto clip = Transform(jittered, {p[0], p[1], p[2], 1.0});
          const auto ndc = centre_ndc(x, y);
          worst = std::max(worst, std::hypot((clip[0] / clip[3] - ndc[0]) * 0.5 * WIDTH, (clip[1] / clip[3] - ndc[1]) * 0.5 * HEIGHT));
          const double distance = std::sqrt(std::pow(p[0] - VIEW_POSITION[0], 2) + std::pow(p[1] - VIEW_POSITION[1], 2) + std::pow(p[2] - VIEW_POSITION[2], 2));
          const double view_z = Transform(VIEW, {p[0], p[1], p[2], 1.0})[2];
          *distance_worst = std::max({*distance_worst, std::abs(p[3] - distance) / distance, std::abs(view_z - g.depth[i] * FAR_PLANE) / view_z});
        }
      }
      return worst;
    };
    const auto [positions, sun] = positions_of(1.f);
    double distance_worst = 0.0, unused = 0.0;
    size_t sky_wrong = 0, unused_sky = 0;
    const double worst = reprojection(positions, &distance_worst, &sky_wrong);
    const double control = reprojection(positions_of(0.f).first, &unused, &unused_sky);
    const double length = std::sqrt(0.25 + 0.5 + 0.25);
    const bool sun_right = std::abs(sun[0][0] - (-0.5 / length)) < 1e-5 && std::abs(sun[0][1] - (0.70710677 / length)) < 1e-5
                           && std::abs(sun[0][2] - (-0.49999994 / length)) < 1e-5 && sun[1][0] == VIEW_POSITION[0] && sun[1][1] == VIEW_POSITION[1]
                           && sun[1][2] == VIEW_POSITION[2] && sun[2][0] == 1.f && sun[2][1] == 0.9658153f && sun[2][2] == 0.45908016f
                           && sun[0][3] == 1.f && sun[2][3] == 1.f;
    std::printf("6. rays' positions projected by the jittered camera: largest distance to the pixel's centre %.3g px; distance and view z\n"
                "   largest relative error %.3g; sky texels not zero %zu; sun texels right %d; control (jitter left in) %.3g px\n",
                worst, distance_worst, sky_wrong, sun_right, control);
    CHECK(worst <= 0.002);
    CHECK(distance_worst <= 2e-5);
    CHECK(sky_wrong == 0 && sun_right);
    CHECK(control > 0.1);
  }

  // Sections 7 to 9: no shadow (the mask's ones), no SSAO (its result 1).
  const std::vector<uint8_t> mask_ones(WIDTH * HEIGHT * 4, 255);
  const auto unshadowed = View(Texture(DXGI_FORMAT_R8G8B8A8_UNORM, WIDTH, HEIGHT, D3D11_BIND_SHADER_RESOURCE, mask_ones.data(), WIDTH * 4).Get());
  // d1_rt_sky as ray_tracing.hpp runs it (one group of 8): the sun texture (16 x 1, its texels first 7) and the copy.
  const auto sky_run = [&](float on) {
    const std::vector<Float4> sentinel(16, Float4{7.f, 7.f, 7.f, 7.f});
    const auto sun = Texture(DXGI_FORMAT_R32G32B32A32_FLOAT, 16, 1, D3D11_BIND_UNORDERED_ACCESS, sentinel.data(), 16 * 16);
    const D3D11_BUFFER_DESC without_desc = {128, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, 0, 0};
    ComPtr<ID3D11Buffer> without;
    REQUIRE(SUCCEEDED(device->CreateBuffer(&without_desc, nullptr, &without)));
    D3D11_UNORDERED_ACCESS_VIEW_DESC without_view = {};
    without_view.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    without_view.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    without_view.Buffer.NumElements = 8;
    ComPtr<ID3D11UnorderedAccessView> without_uav;
    REQUIRE(SUCCEEDED(device->CreateUnorderedAccessView(without.Get(), &without_view, &without_uav)));
    const std::array<float, 4> settings = {on, 0.f, 0.f, 0.f};
    const auto frame_buffer = Constants(PER_FRAME_FULL);
    const auto settings_buffer = Constants(settings);
    const auto sun_uav = Unordered(sun.Get());
    context->ClearState();
    context->CSSetShader(shaders.rt_sky.Get(), nullptr, 0);
    ID3D11Buffer* constants[] = {frame_buffer.Get(), settings_buffer.Get()};
    ID3D11UnorderedAccessView* outputs[] = {sun_uav.Get(), without_uav.Get()};
    context->CSSetConstantBuffers(0, 2, constants);
    context->CSSetUnorderedAccessViews(0, 2, outputs, nullptr);
    context->Dispatch(1, 1, 1);
    return std::pair{ReadFloat4(sun.Get()), without};
  };
  const auto [sky_texels, without_ambient] = sky_run(1.f);

  // 7. Path-Traced Ambient's sky.
  {
    // Each pixel faces a world axis (i % 6: +x, -x, +y, -y, +z, -z): its view-space normal, stereographic (the inverse of
    // the pass's decode).
    GBuffer facing = g;
    for (size_t i = 0; i < facing.normals.size(); ++i) {
      const size_t axis = i % 6;
      std::array<double, 3> world = {};
      world[axis / 2] = (axis % 2 == 0) ? 1.0 : -1.0;
      std::array<double, 3> view = {};
      for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) view[r] += VIEW[r][c] * world[c];
      }
      const double scale = view[2] + 1.0;
      facing.normals[i] = {static_cast<float>((view[0] / scale + 1.7777) / 3.5554), static_cast<float>((view[1] / scale + 1.7777) / 3.5554)};
      facing.depth[i] = 0.1f;
    }
    // R1's sky and ground, its sun and specular off.
    const std::array<float, 16> sky_frame = {-0.5f, 0.29765308f, 0.f, 0.f, 0.70710677f, 0.7011688f, 0.f, 0.f, -0.49999994f, 1.f, 0.f, 0.f,
                                             0.25f, 0.25f, 0.25f, 0.f};
    const auto lit = SunPass(MakeSunInputs(facing, sky_frame, true), no_occlusion.Get(), unshadowed.Get());
    double worst = 0.0, control = 0.0;
    for (size_t i = 0; i < lit.size(); ++i) {
      const size_t axis = i % 6;
      for (int c = 0; c < 3; ++c) {
        worst = std::max(worst, static_cast<double>(std::abs(lit[i][c] - sky_texels[3 + axis][c])));
        control = std::max(control, static_cast<double>(std::abs(lit[i][c] - sky_texels[3 + (axis ^ 1)][c])));
      }
    }
    const bool w_right = sky_texels[3][3] == 1.f && sky_texels[4][3] == 1.f && sky_texels[5][3] == 0.f && sky_texels[6][3] == 0.f
                         && sky_texels[7][3] == 0.f && sky_texels[8][3] == 0.f;
    const auto off = sky_run(0.f).first;
    bool off_zero = true, others_kept = true;
    for (size_t texel = 0; texel < 16; ++texel) {
      for (int c = 0; c < 4; ++c) {
        if (texel >= 3 && texel < 9) {
          off_zero = off_zero && off[texel][c] == 0.f;
        } else {
          others_kept = others_kept && off[texel][c] == 7.f && sky_texels[texel][c] == 7.f;
        }
      }
    }
    std::printf("7. Path-Traced Ambient's sky against the game's sun pass on a white surface facing each axis: largest difference %.3g;\n"
                "   w right %d, zeros with it off %d, other texels untouched %d; control (the opposite axis' texel) %.3g\n",
                worst, w_right, off_zero, others_kept, control);
    CHECK(worst <= 1e-6);
    CHECK(w_right && off_zero && others_kept);
    CHECK(control > 0.1);
  }

  // 8. PerFrame without the ambient (the sun pass's b13 while the paths bring the sky).
  {
    const D3D11_BUFFER_DESC frame_desc = {128, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
    ComPtr<ID3D11Buffer> frame_copy;
    REQUIRE(SUCCEEDED(device->CreateBuffer(&frame_desc, nullptr, &frame_copy)));
    context->CopyResource(frame_copy.Get(), without_ambient.Get());
    std::array<float, 32> zeroed = PER_FRAME_FULL;
    zeroed[1] = zeroed[5] = zeroed[9] = 0.f;
    zeroed[12] = zeroed[13] = zeroed[14] = 0.f;
    const auto copied = ReadBuffer(frame_copy.Get());
    const bool bytes_right = copied.size() == zeroed.size() && std::memcmp(copied.data(), zeroed.data(), sizeof(zeroed)) == 0;
    SunInputs inputs = MakeSunInputs(g, PER_FRAME, false);
    inputs.per_frame = frame_copy;
    const auto ours = SunPass(inputs, no_occlusion.Get(), unshadowed.Get());
    inputs.per_frame = Constants(zeroed);
    const auto reference = SunPass(inputs, no_occlusion.Get(), unshadowed.Get());
    inputs.per_frame = Constants(PER_FRAME_FULL);
    const auto game = SunPass(inputs, no_occlusion.Get(), unshadowed.Get());
    std::printf("8. PerFrame without the ambient: bytes as R1's with the sky and ground zeroed %d; the game's sun pass from it against\n"
                "   from that: largest difference %.3g; control (R1's PerFrame as it is) %.3g\n",
                bytes_right, Largest(ours, reference), Largest(game, reference));
    CHECK(bytes_right);
    CHECK(Largest(ours, reference) == 0.f);
    CHECK(Largest(game, reference) > 1e-2f);
  }

  // 9. The bounce's apply against the game's ambient.
  {
    constexpr UINT SCALE = 2;
    constexpr UINT BOUNCE_WIDTH = WIDTH / SCALE;
    constexpr UINT BOUNCE_HEIGHT = HEIGHT / SCALE;
    constexpr Float4 LIGHT = {0.3f, 0.6f, 0.9f, 1.f};
    // A wall facing the view's z (gBuffer2 0.5, 0.5: the view-space normal 0 0 1; in the world the view's row 2), its
    // points 5 cm apart, 10 m away; the sky pixels (i % 23 == 0) none.
    GBuffer wall = g;
    const std::array<double, 3> normal = {VIEW[2][0], VIEW[2][1], VIEW[2][2]};
    const double across_length = std::hypot(normal[2], normal[0]);
    const std::array<double, 3> across = {normal[2] / across_length, 0.0, -normal[0] / across_length};
    const std::array<double, 3> up = {normal[1] * across[2] - normal[2] * across[1], normal[2] * across[0] - normal[0] * across[2],
                                      normal[0] * across[1] - normal[1] * across[0]};
    const auto point = [&](UINT x, UINT y) {
      Float4 p = {};
      for (int c = 0; c < 3; ++c) p[c] = static_cast<float>(std::array<double, 3>{350.0, 18.0, 300.0}[c] + (x + 0.5) * 0.05 * across[c] + (y + 0.5) * 0.05 * up[c]);
      p[3] = 10.f;
      return p;
    };
    std::vector<Float4> positions(WIDTH * HEIGHT);
    for (UINT y = 0; y < HEIGHT; ++y) {
      for (UINT x = 0; x < WIDTH; ++x) {
        const size_t i = y * WIDTH + x;
        const bool sky = (i % 23 == 0);
        wall.normals[i] = {0.5f, 0.5f};
        wall.depth[i] = sky ? 1.f : 0.1f;
        positions[i] = sky ? Float4{} : point(x, y);
      }
    }
    std::vector<Float4> resolved(BOUNCE_WIDTH * BOUNCE_HEIGHT, LIGHT);
    std::vector<Float4> resolved_positions(BOUNCE_WIDTH * BOUNCE_HEIGHT);
    for (UINT y = 0; y < BOUNCE_HEIGHT; ++y) {
      for (UINT x = 0; x < BOUNCE_WIDTH; ++x) {
        resolved_positions[y * BOUNCE_WIDTH + x] = positions[std::min(y * SCALE + SCALE / 2, HEIGHT - 1) * WIDTH + std::min(x * SCALE + SCALE / 2, WIDTH - 1)];
      }
    }
    const auto float4_view = [](const std::vector<Float4>& texels, UINT width, UINT height) {
      return View(Texture(DXGI_FORMAT_R32G32B32A32_FLOAT, width, height, D3D11_BIND_SHADER_RESOURCE, texels.data(), width * 16).Get());
    };
    const auto resolved_view = float4_view(resolved, BOUNCE_WIDTH, BOUNCE_HEIGHT);
    const auto resolved_positions_view = float4_view(resolved_positions, BOUNCE_WIDTH, BOUNCE_HEIGHT);
    const auto positions_view = float4_view(positions, WIDTH, HEIGHT);
    SunInputs inputs = MakeSunInputs(wall, PER_FRAME, false);
    const std::vector<Float4> no_emissive(WIDTH * HEIGHT, Float4{});
    inputs.emissive = float4_view(no_emissive, WIDTH, HEIGHT);
    struct ApplyConstants {
      std::array<uint32_t, 4> sizes;
      std::array<float, 4> settings;
      std::array<float, 12> view;
    };
    ApplyConstants constants = {.sizes = {BOUNCE_WIDTH, BOUNCE_HEIGHT, WIDTH, HEIGHT}, .settings = {1.5f, static_cast<float>(SCALE), 0.f, 0.f}};
    for (int r = 0; r < 3; ++r) {
      for (int c = 0; c < 3; ++c) constants.view[r * 4 + c] = static_cast<float>(VIEW[r][c]);
    }
    const auto constant_buffer = Constants(constants);
    const auto apply = [&](ID3D11ShaderResourceView* colours) {
      const auto target = Texture(DXGI_FORMAT_R32G32B32A32_FLOAT, WIDTH, HEIGHT, D3D11_BIND_RENDER_TARGET);
      Draw(shaders.full_screen.Get(), shaders.rt_bounce_apply.Get(), Target(target.Get()).Get(), WIDTH, HEIGHT,
           {resolved_view.Get(), resolved_positions_view.Get(), positions_view.Get(), inputs.normals.Get(), colours, inputs.depth.Get()},
           {{0, constant_buffer.Get()}});
      return ReadFloat4(target.Get());
    };
    const auto ours = apply(inputs.diffuse.Get());
    const auto specular_in_place = apply(inputs.specular.Get());
    // The game's ambient of L (sky = ground), its sun and specular off.
    const std::array<float, 16> ambient_frame = {-0.5f, LIGHT[0], 0.f, 0.f, 0.70710677f, LIGHT[1], 0.f, 0.f, -0.49999994f, LIGHT[2], 0.f, 0.f,
                                                 LIGHT[0], LIGHT[1], LIGHT[2], 0.f};
    inputs.per_frame = Constants(ambient_frame);
    const auto game = SunPass(inputs, no_occlusion.Get(), unshadowed.Get());
    double worst = 0.0, control = 0.0;
    size_t sky_lit = 0, lit = 0;
    for (size_t i = 0; i < ours.size(); ++i) {
      if (wall.depth[i] >= 0.999f) {
        sky_lit += (ours[i][0] != 0.f || ours[i][1] != 0.f || ours[i][2] != 0.f) ? 1 : 0;
        continue;
      }
      lit += (game[i][0] + game[i][1] + game[i][2] > 0.f) ? 1 : 0;
      for (int c = 0; c < 3; ++c) {
        worst = std::max(worst, static_cast<double>(std::abs(ours[i][c] - 1.5f * game[i][c])));
        control = std::max(control, static_cast<double>(std::abs(specular_in_place[i][c] - 1.5f * game[i][c])));
      }
    }
    std::printf("9. bounce apply: L x the diffuse colour x 1.5 against 1.5 x the game's sun pass with an ambient of L: largest\n"
                "   difference %.3g over %zu lit pixels; sky pixels lit %zu; control (the specular colours in its place) %.3g\n",
                worst, lit, sky_lit, control);
    CHECK(worst <= 1e-5);
    CHECK(lit > 1000 && sky_lit == 0);
    CHECK(control > 0.05);
  }

  std::printf(failures == 0 ? "tw_d1_rays: all checks passed\n" : "tw_d1_rays: %d checks failed\n", failures);
  return failures == 0 ? 0 : 1;
}
