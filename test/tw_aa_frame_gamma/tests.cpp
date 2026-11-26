// The anti-aliasing passes in the frame's own curve (twattila aa.hlsli AaFrameGamma22: DLAA's aa_dlaa_prepare /
// aa_dlaa_finish, CMAA2's aa_cmaa2_prepare / aa_cmaa2_resolve; Attila and Thrones hand them frame_gamma.hlsli's
// TW_FRAME_GAMMA) on WARP, their built csos against the builds before the change.
// 1. g_aa_frame_gamma 0 (a game that leaves it) and 2 (Gamma Correction off): every output bit the build before's, for
//    each add-on folder given. CONTROL: 2.2 changes them (the constant reaches the passes).
// 2. Round trip at 2.2: prepare then finish (NVIDIA's output = its input) gives the frame back; CMAA2's prepare, its
//    working copy blended (here: kept at 32 bits, not as the prepare stored it), its resolve: the frame back. Grey texels
//    to float rounding (the gamma's own round trip), every texel within 1e-2 (coloured ones near black: the BT.709 /
//    BT.2020 matrices' seven digits steepened by the encode, 2.0's too). The 2.0 round trip beside it. CONTROL: decoded
//    at 2.2, encoded at 2.0: far off.
// 3. g_aa_frame_gamma 1 (aa.hlsli AaFrameLinear, the Denoise Before Tone Mapping row): DLAA's prepare and finish linear,
//    their matrices alone, and the round trip. CONTROL: at 2.0 far off.
// tests.exe <before folder> <after folder> [<before folder> <after folder> ...] (each an add-on's embed folder of csos)
#include <windows.h>
#include <d3d11.h>
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

using Microsoft::WRL::ComPtr;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

namespace {

constexpr UINT WIDTH = 64;
constexpr UINT HEIGHT = 48;

ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> context;

// aa.hlsli's aa_settings.
struct AaSettings {
  std::array<float, 4> metrics = {1.f / WIDTH, 1.f / HEIGHT, static_cast<float>(WIDTH), static_cast<float>(HEIGHT)};
  std::array<float, 7> quality = {};
  float frame_gamma = 0.f;
  std::array<float, 4> subsample = {};
  std::array<float, 4> dlaa_debug = {0.f, static_cast<float>(WIDTH), static_cast<float>(HEIGHT), 0.f};
};

std::vector<char> ReadFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

ComPtr<ID3D11ComputeShader> Shader(const std::string& folder, const char* name) {
  const auto code = ReadFile(folder + "\\" + name + ".cso");
  ComPtr<ID3D11ComputeShader> shader;
  if (code.empty() || FAILED(device->CreateComputeShader(code.data(), code.size(), nullptr, &shader))) {
    std::printf("not loaded: %s\\%s.cso (%zu bytes)\n", folder.c_str(), name, code.size());
    return {};
  }
  return shader;
}

struct Texture {
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> srv;
  ComPtr<ID3D11UnorderedAccessView> uav;
};

Texture MakeTexture(DXGI_FORMAT format, const void* data, UINT pixel_bytes) {
  Texture made;
  const D3D11_TEXTURE2D_DESC desc = {WIDTH, HEIGHT, 1, 1, format, {1, 0}, D3D11_USAGE_DEFAULT,
                                     D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0, 0};
  const D3D11_SUBRESOURCE_DATA init = {data, WIDTH * pixel_bytes, 0};
  device->CreateTexture2D(&desc, data != nullptr ? &init : nullptr, &made.texture);
  device->CreateShaderResourceView(made.texture.Get(), nullptr, &made.srv);
  device->CreateUnorderedAccessView(made.texture.Get(), nullptr, &made.uav);
  return made;
}

std::vector<float> ReadBack(ID3D11Texture2D* texture) {
  D3D11_TEXTURE2D_DESC desc;
  texture->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING, desc.BindFlags = 0, desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  device->CreateTexture2D(&desc, nullptr, &staging);
  context->CopyResource(staging.Get(), texture);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  std::vector<float> values(size_t{WIDTH} * HEIGHT * 4);
  for (UINT y = 0; y < HEIGHT; ++y) std::memcpy(values.data() + (size_t{y} * WIDTH * 4), static_cast<const char*>(mapped.pData) + (size_t{y} * mapped.RowPitch), WIDTH * 16);
  context->Unmap(staging.Get(), 0);
  return values;
}

void Dispatch(ID3D11ComputeShader* shader, float frame_gamma, std::initializer_list<ID3D11ShaderResourceView*> views,
              std::initializer_list<ID3D11UnorderedAccessView*> outputs) {
  AaSettings settings;
  settings.frame_gamma = frame_gamma;
  const D3D11_BUFFER_DESC desc = {sizeof(AaSettings) + 15u & ~15u, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
  std::array<char, (sizeof(AaSettings) + 15u) & ~15u> bytes = {};
  std::memcpy(bytes.data(), &settings, sizeof(settings));
  const D3D11_SUBRESOURCE_DATA init = {bytes.data(), 0, 0};
  ComPtr<ID3D11Buffer> buffer;
  device->CreateBuffer(&desc, &init, &buffer);
  // aa_taa (b1): zeros (the prepare's motion then 0; its colour alone is compared).
  std::array<float, 64> taa = {};
  const D3D11_BUFFER_DESC taa_desc = {sizeof(taa), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
  const D3D11_SUBRESOURCE_DATA taa_init = {taa.data(), 0, 0};
  ComPtr<ID3D11Buffer> taa_buffer;
  device->CreateBuffer(&taa_desc, &taa_init, &taa_buffer);
  const std::array<ID3D11Buffer*, 2> buffers = {buffer.Get(), taa_buffer.Get()};
  context->CSSetConstantBuffers(0, 2, buffers.data());
  context->CSSetShaderResources(0, static_cast<UINT>(views.size()), views.begin());
  context->CSSetUnorderedAccessViews(0, static_cast<UINT>(outputs.size()), outputs.begin(), nullptr);
  context->CSSetShader(shader, nullptr, 0);
  context->Dispatch((WIDTH + 7) / 8, (HEIGHT + 7) / 8, 1);
  context->ClearState();
}

// Fixed noise.
float Noise(uint32_t a, uint32_t b) {
  uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u;
  h ^= h >> 15, h *= 0x2C1B3C6Du, h ^= h >> 12;
  return static_cast<float>(h & 0xFFFFFFu) / 16777215.f;
}

struct Passes {
  ComPtr<ID3D11ComputeShader> dlaa_prepare, dlaa_finish, cmaa2_prepare, cmaa2_resolve;
  bool Load(const std::string& folder) {
    dlaa_prepare = Shader(folder, "aa_dlaa_prepare");
    dlaa_finish = Shader(folder, "aa_dlaa_finish");
    cmaa2_prepare = Shader(folder, "aa_cmaa2_prepare");
    cmaa2_resolve = Shader(folder, "aa_cmaa2_resolve");
    return dlaa_prepare && dlaa_finish && cmaa2_prepare && cmaa2_resolve;
  }
};

// The four passes' outputs from `frame` (2.0 encoded, signed) at `frame_gamma`: DLAA's prepare, its finish of `nvidia`,
// CMAA2's prepare, its resolve of `work`.
std::array<std::vector<float>, 4> Outputs(const Passes& passes, float frame_gamma, const Texture& frame, const Texture& depth, const Texture& nvidia,
                                         const Texture& work) {
  Texture prepared = MakeTexture(DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 16);
  Texture prepared_depth = MakeTexture(DXGI_FORMAT_R32_FLOAT, nullptr, 4);
  Texture motion = MakeTexture(DXGI_FORMAT_R32G32_FLOAT, nullptr, 8);
  Dispatch(passes.dlaa_prepare.Get(), frame_gamma, {frame.srv.Get(), depth.srv.Get()}, {prepared.uav.Get(), prepared_depth.uav.Get(), motion.uav.Get()});
  Texture finished = MakeTexture(DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 16);
  Dispatch(passes.dlaa_finish.Get(), frame_gamma, {nvidia.srv.Get(), frame.srv.Get()}, {finished.uav.Get()});
  Texture cmaa2_work = MakeTexture(DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 16);
  Dispatch(passes.cmaa2_prepare.Get(), frame_gamma, {frame.srv.Get()}, {cmaa2_work.uav.Get()});
  Texture resolved = MakeTexture(DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 16);
  Dispatch(passes.cmaa2_resolve.Get(), frame_gamma, {work.srv.Get(), frame.srv.Get()}, {resolved.uav.Get()});
  return {ReadBack(prepared.texture.Get()), ReadBack(finished.texture.Get()), ReadBack(cmaa2_work.texture.Get()), ReadBack(resolved.texture.Get())};
}

bool SameBits(const std::vector<float>& a, const std::vector<float>& b) {
  return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

// The largest difference in the colour channels (`grey`: of the texels grey in `a` alone).
double Worst(const std::vector<float>& a, const std::vector<float>& b, bool grey = false) {
  double worst = 0;
  for (size_t texel = 0; texel < a.size(); texel += 4) {
    if (grey && (a[texel] != a[texel + 1] || a[texel] != a[texel + 2])) continue;
    for (size_t c = 0; c < 3; ++c) worst = std::max(worst, std::abs(static_cast<double>(a[texel + c]) - b[texel + c]));
  }
  return worst;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3 || argc % 2 == 0) {
    std::printf("usage: tests.exe <before folder> <after folder> [...]\n");
    return 2;
  }
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)));

  // The tone mapper's output: 2.0 encoded, signed (wide colours below 0) up to 1.6 (HDR over Paper White); its depth;
  // NVIDIA's output (BT.2020, linear, positive) and a blended CMAA2 working copy (BT.2020, linear).
  std::vector<float> frame_texels(size_t{WIDTH} * HEIGHT * 4);
  std::vector<float> nvidia_texels(frame_texels.size());
  std::vector<float> depth_texels(size_t{WIDTH} * HEIGHT);
  for (size_t i = 0; i < frame_texels.size(); ++i) {
    frame_texels[i] = (i % 4 == 3) ? 1.f : (1.8f * Noise(static_cast<uint32_t>(i), 1) - 0.2f);
    nvidia_texels[i] = (i % 4 == 3) ? 1.f : 2.f * Noise(static_cast<uint32_t>(i), 2);
  }
  for (size_t i = 0; i < depth_texels.size(); ++i) depth_texels[i] = Noise(static_cast<uint32_t>(i), 3);
  const Texture frame = MakeTexture(DXGI_FORMAT_R32G32B32A32_FLOAT, frame_texels.data(), 16);
  const Texture nvidia = MakeTexture(DXGI_FORMAT_R32G32B32A32_FLOAT, nvidia_texels.data(), 16);
  const Texture depth = MakeTexture(DXGI_FORMAT_R32_FLOAT, depth_texels.data(), 4);

  // 1. Each folder pair: 0 and 2 the build before's bits; 2.2 not.
  const std::array<const char*, 4> names = {"aa_dlaa_prepare", "aa_dlaa_finish", "aa_cmaa2_prepare", "aa_cmaa2_resolve"};
  for (int pair = 1; pair + 1 < argc; pair += 2) {
    Passes before;
    Passes after;
    CHECK(before.Load(argv[pair]) && after.Load(argv[pair + 1]));
    const auto old_outputs = Outputs(before, 0.f, frame, depth, nvidia, nvidia);
    for (const float gamma : {0.f, 2.f}) {
      const auto new_outputs = Outputs(after, gamma, frame, depth, nvidia, nvidia);
      for (size_t pass = 0; pass < 4; ++pass) CHECK(SameBits(old_outputs[pass], new_outputs[pass]));
    }
    const auto corrected = Outputs(after, 2.2f, frame, depth, nvidia, nvidia);
    std::printf("1 %s: g_aa_frame_gamma 0 and 2: all four passes' bits the build before's; CONTROL 2.2:", argv[pair + 1]);
    for (size_t pass = 0; pass < 4; ++pass) {
      CHECK(!SameBits(old_outputs[pass], corrected[pass]));
      std::printf(" %s %.3g", names[pass], Worst(old_outputs[pass], corrected[pass]));
    }
    std::printf("\n");
  }

  // 2. Round trips through the last folder's passes, in-gamut colours (the prepares keep BT.2020's positive part).
  Passes passes;
  CHECK(passes.Load(argv[argc - 1]));
  std::vector<float> in_gamut(frame_texels.size());
  // Every other texel grey (the matrices keep grey grey: there the round trip is the gamma's alone).
  for (size_t i = 0; i < in_gamut.size(); ++i) {
    const size_t texel = i / 4;
    in_gamut[i] = (i % 4 == 3) ? 1.f : 1.6f * Noise(static_cast<uint32_t>((texel % 2 == 0) ? i : texel * 4), 4);
  }
  const Texture source = MakeTexture(DXGI_FORMAT_R32G32B32A32_FLOAT, in_gamut.data(), 16);
  const auto round_trip = [&](float decode_gamma, float encode_gamma) {
    // DLAA: the prepare's colour as NVIDIA's output; CMAA2: its working copy at 32 bits (so its resolve encodes every pixel).
    Texture prepared = MakeTexture(DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 16);
    Texture prepared_depth = MakeTexture(DXGI_FORMAT_R32_FLOAT, nullptr, 4);
    Texture motion = MakeTexture(DXGI_FORMAT_R32G32_FLOAT, nullptr, 8);
    Dispatch(passes.dlaa_prepare.Get(), decode_gamma, {source.srv.Get(), depth.srv.Get()}, {prepared.uav.Get(), prepared_depth.uav.Get(), motion.uav.Get()});
    Texture finished = MakeTexture(DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 16);
    Dispatch(passes.dlaa_finish.Get(), encode_gamma, {prepared.srv.Get(), source.srv.Get()}, {finished.uav.Get()});
    Texture work = MakeTexture(DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 16);
    Dispatch(passes.cmaa2_prepare.Get(), decode_gamma, {source.srv.Get()}, {work.uav.Get()});
    Texture resolved = MakeTexture(DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 16);
    Dispatch(passes.cmaa2_resolve.Get(), encode_gamma, {work.srv.Get(), source.srv.Get()}, {resolved.uav.Get()});
    const auto dlaa = ReadBack(finished.texture.Get());
    const auto cmaa2 = ReadBack(resolved.texture.Get());
    // Grey texels, and every texel (coloured ones near black: the BT.709 / BT.2020 matrices' seven digits, steepened by
    // the encode, as at 2.0).
    return std::array<double, 4>{Worst(in_gamut, dlaa, true), Worst(in_gamut, cmaa2, true), Worst(in_gamut, dlaa), Worst(in_gamut, cmaa2)};
  };
  const auto at_22 = round_trip(2.2f, 2.2f);
  const auto at_20 = round_trip(2.f, 2.f);
  const auto mixed = round_trip(2.2f, 2.f);
  std::printf("2 round trip, worst difference from the frame (0 to 1.6), grey / every texel: at 2.2 DLAA %.3g / %.3g, "
              "CMAA2 %.3g / %.3g; at 2.0 DLAA %.3g / %.3g, CMAA2 %.3g / %.3g; CONTROL decoded at 2.2, encoded at 2.0: DLAA %.3g, CMAA2 %.3g\n",
              at_22[0], at_22[2], at_22[1], at_22[3], at_20[0], at_20[2], at_20[1], at_20[3], mixed[0], mixed[1]);
  CHECK(at_22[0] < 1e-5 && at_22[1] < 1e-5);
  CHECK(at_22[2] < 1e-2 && at_22[3] < 1e-2);
  CHECK(mixed[0] > 1e-2 && mixed[1] > 1e-2);

  // 3. g_aa_frame_gamma 1 (aa.hlsli AaFrameLinear: Ray Reconstruction on the lit scene before the tone mapper, no square
  // root): DLAA's prepare gives NVIDIA the frame as it is (BT.2020, its positive part), its finish NVIDIA's output as it
  // is (BT.709), the round trip the frame. CONTROL: at 2.0 the prepare squares and the finish takes the square root.
  const auto linear_pass = [&](float frame_gamma) {
    Texture prepared = MakeTexture(DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 16);
    Texture prepared_depth = MakeTexture(DXGI_FORMAT_R32_FLOAT, nullptr, 4);
    Texture motion = MakeTexture(DXGI_FORMAT_R32G32_FLOAT, nullptr, 8);
    Dispatch(passes.dlaa_prepare.Get(), frame_gamma, {source.srv.Get(), depth.srv.Get()}, {prepared.uav.Get(), prepared_depth.uav.Get(), motion.uav.Get()});
    Texture finished = MakeTexture(DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 16);
    Dispatch(passes.dlaa_finish.Get(), frame_gamma, {nvidia.srv.Get(), source.srv.Get()}, {finished.uav.Get()});
    Texture trip = MakeTexture(DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 16);
    Dispatch(passes.dlaa_finish.Get(), frame_gamma, {prepared.srv.Get(), source.srv.Get()}, {trip.uav.Get()});
    return std::array<std::vector<float>, 3>{ReadBack(prepared.texture.Get()), ReadBack(finished.texture.Get()), ReadBack(trip.texture.Get())};
  };
  std::vector<float> expected_prepared(in_gamut.size());
  std::vector<float> expected_finished(in_gamut.size());
  for (size_t texel = 0; texel < in_gamut.size(); texel += 4) {
    const float* c = &in_gamut[texel];
    const float* n = &nvidia_texels[texel];
    expected_prepared[texel + 0] = std::max(0.6274040f * c[0] + 0.3292820f * c[1] + 0.0433136f * c[2], 0.f);
    expected_prepared[texel + 1] = std::max(0.0690970f * c[0] + 0.9195400f * c[1] + 0.0113612f * c[2], 0.f);
    expected_prepared[texel + 2] = std::max(0.0163916f * c[0] + 0.0880132f * c[1] + 0.8955950f * c[2], 0.f);
    expected_finished[texel + 0] = 1.6604910f * n[0] - 0.5876411f * n[1] - 0.0728499f * n[2];
    expected_finished[texel + 1] = -0.1245505f * n[0] + 1.1328999f * n[1] - 0.0083494f * n[2];
    expected_finished[texel + 2] = -0.0181508f * n[0] - 0.1005789f * n[1] + 1.1187297f * n[2];
  }
  const auto linear = linear_pass(1.f);
  const auto encoded = linear_pass(2.f);
  const std::array<double, 4> linear_worst = {Worst(expected_prepared, linear[0]), Worst(expected_finished, linear[1]),
                                              Worst(in_gamut, linear[2], true), Worst(in_gamut, linear[2])};
  const std::array<double, 2> control_worst = {Worst(expected_prepared, encoded[0]), Worst(expected_finished, encoded[1])};
  std::printf("3 g_aa_frame_gamma 1, worst difference: prepare vs the frame in BT.2020 %.3g, finish vs NVIDIA's in BT.709 %.3g, "
              "round trip grey / every texel %.3g / %.3g; CONTROL at 2.0: prepare %.3g, finish %.3g\n",
              linear_worst[0], linear_worst[1], linear_worst[2], linear_worst[3], control_worst[0], control_worst[1]);
  CHECK(linear_worst[0] < 1e-5 && linear_worst[1] < 1e-5 && linear_worst[2] < 1e-5 && linear_worst[3] < 1e-4);
  CHECK(control_worst[0] > 1e-2 && control_worst[1] > 1e-2);
  std::printf("PASS\n");
  return 0;
}
