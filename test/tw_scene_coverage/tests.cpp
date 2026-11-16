// twattila/rt_core.hpp's scene_draw hosts' masked draws (SceneCoverage; Tales of Arise's prepass), on WARP with the real
// rt_core: a masked pixel shader (t0's alpha at a TEXCOORD, discarded below 0.3333, as Tales' masked prepass) bound, its
// coverage as OnInitPipeline classifies it.
// 1. Host::leaf_cutoff: a host with 0.333 gives its masked draws that cut-off (SceneShader::alpha_cutoff: their captures
//    RT_OWN_CUTOFF) and the bridge's rt_leaf_cutoff 0.333, through InstallScene (scene_draw hosts) and
//    InstallReplayedScene (R.U.S.E.). Default (every current host): alpha_cutoff left as scene_draw set it (0, or a value
//    of its own) and the bridge's 0 (the helper's 0.5), as before; listed shaders' own cut-off still taken. CONTROL: the
//    rule before (the listed shaders' largest, 0 for a scene_draw host) gives 0 for the 0.333 host.
#include <d3dcompiler.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>

#include "src/games/twattila/rt_core.hpp"

using Microsoft::WRL::ComPtr;

extern "C" __declspec(dllexport) const char* const NAME = "Scene coverage test";
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}

namespace core = twattila::rt_scene::internal;
using twattila::rt_scene::Host;
using twattila::rt_scene::SceneShader;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

namespace {

ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> context;

// `source` compiled as the build compiles (O3), bound as the context's pixel shader and classified as OnInitPipeline does.
ComPtr<ID3D11PixelShader> Bind(const char* source, core::PixelCoverage* coverage) {
  ComPtr<ID3DBlob> code;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompile(source, std::strlen(source), "test", nullptr, nullptr, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors))) {
    std::printf("compile failed: %s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
    return {};
  }
  ComPtr<ID3D11PixelShader> shader;
  if (FAILED(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader))) return {};
  *coverage = core::ClassifyCoverage({static_cast<const uint8_t*>(code->GetBufferPointer()), code->GetBufferSize()});
  const std::unique_lock lock(core::pixel_coverage_mutex);
  core::pixel_coverage[reinterpret_cast<uint64_t>(shader.Get())] = *coverage;
  context->PSSetShader(shader.Get(), nullptr, 0);
  return shader;
}

bool NoDraw(ID3D11DeviceContext*, SceneShader*) { return false; }

// Tales' masked prepass shape: t0's alpha at TEXCOORD0, biased, x a material factor, discarded below 0.3333.
constexpr const char* MASKED = R"(
Texture2D<float4> t0 : register(t0);
SamplerState s0 : register(s0);
cbuffer cb1 : register(b1) { float4 material[10]; }
void main(float4 position : SV_Position, float2 uv : TEXCOORD0) {
  if (t0.SampleBias(s0, uv, -1.f).w * material[9].y - 0.3333f < 0.f) discard;
})";

}  // namespace

int main() {
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)));
  core::PixelCoverage coverage;
  const auto masked = Bind(MASKED, &coverage);
  CHECK(masked && coverage.coverage == core::Coverage::MASKED && coverage.uv_semantic == "TEXCOORD" && coverage.uv_index == 0);

  // 1. Host::leaf_cutoff.
  const std::array<SceneShader, 2> listed = {SceneShader{.hash = 1, .alpha_cutoff = 0.01f}, SceneShader{.hash = 2}};
  for (const bool replayed : {false, true}) {
    const auto install = [replayed](const Host& host, std::span<const SceneShader> shaders) {
      if (replayed) {
        twattila::rt_scene::InstallReplayedScene(host);
      } else {
        twattila::rt_scene::InstallScene(shaders, host);
      }
    };
    install(Host{.scene_draw = NoDraw, .leaf_cutoff = 0.333f}, {});
    SceneShader shader;
    CHECK(twattila::rt_scene::SceneCoverage(context.Get(), &shader));
    CHECK(shader.foliage && shader.alpha_cutoff == 0.333f && core::leaf_cutoff == 0.333f);
    // CONTROL: the rule before for this host (no listed shaders: 0).
    const float before = 0.f;
    CHECK(before != core::leaf_cutoff);
    // Default: as before.
    install(Host{.scene_draw = NoDraw}, {});
    SceneShader plain;
    SceneShader own = {.alpha_cutoff = 0.01f};
    CHECK(twattila::rt_scene::SceneCoverage(context.Get(), &plain) && twattila::rt_scene::SceneCoverage(context.Get(), &own));
    CHECK(plain.foliage && plain.alpha_cutoff == 0.f && own.alpha_cutoff == 0.01f && core::leaf_cutoff == 0.f);
    std::printf("part 1 (%s): host 0.333: draw %.3f, bridge %.3f (CONTROL the rule before: %.3f); default: draw %.3f / own %.3f kept, bridge %.3f\n",
                replayed ? "InstallReplayedScene" : "InstallScene", shader.alpha_cutoff, 0.333f, before, plain.alpha_cutoff, own.alpha_cutoff,
                core::leaf_cutoff);
  }
  // Listed shaders (the TW games: no scene_draw): their largest cut-off, as before.
  twattila::rt_scene::InstallScene(listed, Host{});
  CHECK(core::leaf_cutoff == 0.01f);
  twattila::rt_scene::InstallScene(listed, Host{.leaf_cutoff = 0.333f});
  CHECK(core::leaf_cutoff == 0.333f);
  std::printf("part 1: listed shaders 0.01 -> bridge %.3f as before; with the host's 0.333 the larger\n", 0.01f);
  std::printf("PASS\n");
  return 0;
}
