// PsychoV30's NaN / infinity guards (twattila/psycho_test30.hlsli) on WARP, compiled with the build's flags (/Gec, no
// /Gis). fxc warns X3577 ("value cannot be NaN, isnan() may not be necessary") at some of them: there it has proven the
// value finite (a constant argument, a clamped one) and drops the test; elsewhere it keeps isnan as `ne x, x` and isinf
// as a bit test. v30.hlsl as shipped against the control: the same file with every isnan / isinf false (NO_GUARDS).
// 1. Finite inputs (grey ramp, 48 hues, fire, out-of-gamut and extreme values) x 15 slider sets (defaults, SDR output,
//    each slider at its ends): the guards are live in the whole curve (taking them out changes some outputs: the
//    control must differ), and ours is finite.
// 2. Ours finite everywhere, the NaN / infinity inputs included (the control's count reported).
// 3. The guarded steps on a NaN / infinity computed in the shader: ours flags each one, the control (must fail) misses some.
// tests.exe <ours cso> <control cso> <ours guards cso> <control guards cso>; exit code 0 when every check holds.
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;
using Float4 = std::array<float, 4>;

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (false)
#define REQUIRE(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;

// v30.hlsl's Case.
struct Case {
  Float4 color;  // rgb, peak
  Float4 grade;  // exposure, highlights, shadows, contrast
  Float4 extra;  // purity, cone response exponent, gamut mode, anchor
};

static std::vector<char> ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static std::vector<Float4> Run(const std::filesystem::path& path, const std::vector<Case>& cases) {
  const std::vector<char> code = ReadFile(path);
  REQUIRE(!code.empty());
  ComPtr<ID3D11ComputeShader> shader;
  REQUIRE(SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, &shader)));
  const auto count = static_cast<UINT>(cases.size());
  const D3D11_BUFFER_DESC input_desc = {static_cast<UINT>(count * sizeof(Case)), D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0,
                                        D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, sizeof(Case)};
  const D3D11_SUBRESOURCE_DATA input_init = {cases.data(), 0, 0};
  ComPtr<ID3D11Buffer> input;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&input_desc, &input_init, &input)));
  ComPtr<ID3D11ShaderResourceView> input_view;
  REQUIRE(SUCCEEDED(device->CreateShaderResourceView(input.Get(), nullptr, &input_view)));
  const D3D11_BUFFER_DESC output_desc = {static_cast<UINT>(count * sizeof(Float4)), D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0,
                                         D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, sizeof(Float4)};
  ComPtr<ID3D11Buffer> output;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&output_desc, nullptr, &output)));
  ComPtr<ID3D11UnorderedAccessView> output_view;
  REQUIRE(SUCCEEDED(device->CreateUnorderedAccessView(output.Get(), nullptr, &output_view)));
  context->ClearState();
  context->CSSetShader(shader.Get(), nullptr, 0);
  context->CSSetShaderResources(0, 1, input_view.GetAddressOf());
  context->CSSetUnorderedAccessViews(0, 1, output_view.GetAddressOf(), nullptr);
  context->Dispatch((count + 63) / 64, 1, 1);
  const D3D11_BUFFER_DESC staging_desc = {static_cast<UINT>(count * sizeof(Float4)), D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0};
  ComPtr<ID3D11Buffer> staging;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&staging_desc, nullptr, &staging)));
  context->CopyResource(staging.Get(), output.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  REQUIRE(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
  std::vector<Float4> results(count);
  std::memcpy(results.data(), mapped.pData, count * sizeof(Float4));
  context->Unmap(staging.Get(), 0);
  return results;
}

static bool Finite(const Float4& value) {
  return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}

int main(int argc, char** argv) {
  REQUIRE(argc >= 5);
  REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)));

  // Colours: grey 2^-14 - 2^14, 48 hues at three levels, fire, out-of-gamut, black, the float range's ends.
  std::vector<std::array<float, 3>> colours;
  for (int stop = -14; stop <= 14; ++stop) colours.push_back({std::exp2f(stop) * 0.18f, std::exp2f(stop) * 0.18f, std::exp2f(stop) * 0.18f});
  for (int i = 0; i < 48; ++i) {
    const float hue = i / 48.f;
    const auto channel = [hue](float n) {
      const float k = std::fmod(n + hue * 6.f, 6.f);
      return 1.f - std::max(0.f, std::min({k, 4.f - k, 1.f}));
    };
    for (const float level : {0.05f, 1.f, 20.f}) colours.push_back({channel(5.f) * level, channel(3.f) * level, channel(1.f) * level});
  }
  for (const float level : {1.f, 10.f, 100.f}) colours.push_back({level, 0.4f * level, 0.1f * level});
  colours.insert(colours.end(), {{-0.05f, 0.5f, 1.f}, {0.9f, -0.02f, 0.01f}, {-0.1f, -0.1f, -0.1f}, {0.f, 0.f, 0.f},
                                 {1e-20f, 1e-20f, 1e-20f}, {1e20f, 1e20f, 1e20f}, {3e38f, 1.f, 0.f}, {-3e38f, 0.5f, 0.5f}});
  constexpr float NAN_VALUE = std::numeric_limits<float>::quiet_NaN();
  constexpr float INF_VALUE = std::numeric_limits<float>::infinity();
  const std::vector<std::array<float, 3>> bad = {{NAN_VALUE, 0.5f, 0.5f}, {NAN_VALUE, NAN_VALUE, NAN_VALUE}, {0.5f, INF_VALUE, 0.5f},
                                                  {0.5f, 0.5f, -INF_VALUE}, {INF_VALUE, INF_VALUE, INF_VALUE}, {NAN_VALUE, INF_VALUE, -INF_VALUE}};

  // Slider sets: peak, exposure, highlights, shadows, contrast, purity, cone, gamut mode.
  struct Sliders {
    float peak = 1000.f / 203.f, exposure = 1.f, highlights = 1.f, shadows = 1.f, contrast = 1.f, purity = 1.f, cone = 1.f, gamut = 1.f;
  };
  std::vector<Sliders> sliders = {{}, {.peak = 1.f, .gamut = 0.f}, {.peak = 49.f}};
  for (const float end : {0.f, 2.f}) {
    sliders.push_back({.exposure = end});
    sliders.push_back({.highlights = end});
    sliders.push_back({.shadows = end});
    sliders.push_back({.contrast = (end == 0.f ? 0.01f : end)});
    sliders.push_back({.purity = end});
    sliders.push_back({.cone = (end == 0.f ? 0.01f : end)});
  }

  std::vector<Case> cases;
  for (const Sliders& set : sliders) {
    for (const auto& colour : colours) {
      cases.push_back({{colour[0], colour[1], colour[2], set.peak}, {set.exposure, set.highlights, set.shadows, set.contrast},
                       {set.purity, set.cone, set.gamut, 0.18f}});
    }
  }
  const size_t finite_count = cases.size();
  for (const Sliders& set : sliders) {
    for (const auto& colour : bad) {
      cases.push_back({{colour[0], colour[1], colour[2], set.peak}, {set.exposure, set.highlights, set.shadows, set.contrast},
                       {set.purity, set.cone, set.gamut, 0.18f}});
    }
  }

  const auto ours = Run(argv[1], cases);
  const auto control = Run(argv[2], cases);

  // 1. Finite inputs: where taking the guards out changes the curve's output (they are compiled in, not folded away).
  int changed = 0;
  for (size_t i = 0; i < finite_count; ++i) {
    float difference = 0.f;
    for (int c = 0; c < 3; ++c) difference = std::max(difference, std::abs(ours[i][c] - control[i][c]) / std::max(1.f, std::abs(control[i][c])));
    if (!(difference <= 1e-6f)) {
      if (changed < 4) {
        std::printf("   guards changed: in (%g, %g, %g) peak %g, sliders (%g %g %g %g | %g %g %g): ours (%g, %g, %g), control (%g, %g, %g)\n",
                    cases[i].color[0], cases[i].color[1], cases[i].color[2], cases[i].color[3], cases[i].grade[0], cases[i].grade[1],
                    cases[i].grade[2], cases[i].grade[3], cases[i].extra[0], cases[i].extra[1], cases[i].extra[2], ours[i][0], ours[i][1],
                    ours[i][2], control[i][0], control[i][1], control[i][2]);
      }
      ++changed;
    }
  }
  std::printf("1. finite inputs: taking the guards out changes %d of %zu outputs\n", changed, finite_count);
  CHECK(changed > 0);

  // 2. Ours finite everywhere (NaN inputs become 0, infinite ones the largest finite input, as the file says).
  int ours_bad = 0;
  int control_bad = 0;
  for (size_t i = 0; i < cases.size(); ++i) {
    if (!Finite(ours[i])) {
      if (ours_bad < 5) std::printf("   ours non-finite: case %zu in (%g, %g, %g) out (%g, %g, %g)\n", i, cases[i].color[0], cases[i].color[1],
                                    cases[i].color[2], ours[i][0], ours[i][1], ours[i][2]);
      ++ours_bad;
    }
    if (!Finite(control[i])) ++control_bad;
  }
  std::printf("2. ours: %d of %zu outputs non-finite (%zu NaN / infinity inputs); the control (no guards) %d\n", ours_bad,
              cases.size(), cases.size() - finite_count, control_bad);
  CHECK(ours_bad == 0);

  // 3. The guarded steps on a NaN / infinity computed in the shader (v30.hlsl's guards: rgb / grade.xyz): ours flags every
  //    one invalid (and keeps the ceiling solve finite, rejects an infinite total); the control must miss some.
  const std::vector<std::pair<std::array<float, 3>, std::array<float, 3>>> divisions = {
      {{0.5f, 0.4f, 0.3f}, {1.f, 1.f, 1.f}},    // finite: valid
      {{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}},       // NaN
      {{0.f, 0.4f, 0.3f}, {0.f, 1.f, 1.f}},     // NaN in one channel
      {{1.f, 0.4f, 0.3f}, {0.f, 1.f, 1.f}},     // +infinity
      {{0.5f, 0.4f, -1.f}, {1.f, 1.f, 0.f}},    // -infinity
      {{1.f, 1.f, 1.f}, {0.f, 0.f, 0.f}},       // +infinity everywhere
  };
  std::vector<Case> guard_cases;
  for (const auto& [rgb, divisor] : divisions) guard_cases.push_back({{rgb[0], rgb[1], rgb[2], 1.f}, {divisor[0], divisor[1], divisor[2], 1.f}, {}});
  const auto ours_guards = Run(argv[3], guard_cases);
  const auto control_guards = Run(argv[4], guard_cases);
  int control_missed = 0;
  bool ours_right = ours_guards[0] == Float4{1.f, 1.f, 1.f, 1.f} && control_guards[0] == Float4{1.f, 1.f, 1.f, 1.f};
  for (size_t i = 1; i < guard_cases.size(); ++i) {
    std::printf("   %zu: MB valid / ceiling valid / total kept / ceiling finite: ours %.0f %.0f %.0f %.0f, control %.0f %.0f %.0f %.0f\n", i,
                ours_guards[i][0], ours_guards[i][1], ours_guards[i][2], ours_guards[i][3], control_guards[i][0], control_guards[i][1],
                control_guards[i][2], control_guards[i][3]);
    ours_right = ours_right && ours_guards[i][0] == 0.f && ours_guards[i][1] == 0.f && ours_guards[i][3] == 1.f;
    control_missed += (control_guards[i][0] != 0.f) + (control_guards[i][1] != 0.f) + (control_guards[i][3] != 1.f);
  }
  // An infinite total is rejected (a NaN one turns 0 at the guard's max(rgb, 0) already).
  ours_right = ours_right && ours_guards[3][2] == 0.f && ours_guards[5][2] == 0.f;
  control_missed += (control_guards[3][2] != 0.f) + (control_guards[5][2] != 0.f);
  std::printf("3. guarded steps: ours %s; the control (no guards) missed %d of %zu\n", ours_right ? "right in every case" : "WRONG",
              control_missed, (guard_cases.size() - 1) * 3 + 2);
  CHECK(ours_right);
  CHECK(control_missed > 0);

  std::printf(failures == 0 ? "tw_psychov30_nan: all checks passed\n" : "tw_psychov30_nan: %d checks failed\n", failures);
  return failures == 0 ? 0 : 1;
}
