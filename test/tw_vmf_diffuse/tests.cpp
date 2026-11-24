/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// vMF diffuse S1 (designs/vmf_diffuse.md section 9, vmf_s0/s0_report.md 7-8): src/games/twattila/vmf_diffuse.hlsli
// against its CPU twin (below: a float port of vmf_s0.cpp's Model / SigmaEq / Coeffs / FmTerm with the x2 series, the
// fm ramp, R clamped to [0, 4] over the clamped base, c <= 0.999 and the u_v fade), and the twin against S0's walk and
// model. Both take their constants and S0's tables from the one src/games/twattila/vmf_diffuse.h. The header runs in
// vmf_probe.hlsl on WARP, compiled by fxc (cs_5_0, D3D11) and dxc (cs_6_5, D3D12); dxc's cs_6_9 build is checked
// statically only.
// Gates (each PASS / FAIL line):
//   erf     Abramowitz-Stegun 7.1.26 (the header's VmfErf) against std::erf: |error| <= 1e-6 on [-6, 6]
//   T0p     the twin against S0's double model at T0's points (r 0.005 / 0.05 / 0.15 / 0.26 / 0.40, c 0.1 / 0.5 / 0.8,
//           u >= 0.2): relative <= 1e-4 (the port)
//   T0w     the twin against S0's walk there: rms <= 3 % per (r, c) cell (s0_report 7.5)
//   T2      reciprocity |R(l, v) - R(v, l)| <= 1e-5 max(R, 1) for u >= 0.1; R in [0, 4]; the beta table's bond albedo
//           (cosine-weighted mean of the directional albedo of base x R) within 1 % of base, r 0.005-0.40, base
//           0.05-0.95; Eq. 8's denominator >= 0.5 u_l u_v over u in [1e-4, 1]^2, r in (0, 0.40] (the minimum
//           printed: 0.62); vmf_diffuse.h's beta
//           table at S0's 80 check points (s0_points.h, from beta_check.csv) within 2e-6
//   T3      r 0, slider 0 and rho 0: exactly 1.0f (VmfLobe and VmfLobeFast) on the twin and on both WARP builds
//   T4      the NaN / Inf sweep (u -0.3 / 0 / 1e-8 / 1e-4 / 0.03 / 0.05 / 0.08 / 0.1 / 1, l.v -1 / 1, r 1e-6 / the
//           Eq. 4-5 and 5-6 seams / 0.40 and 1e-12 / 1e-30 past them, base 0 / 1e-5 / 0.999 / 1 / 1.5, erf at 0 / 1 /
//           large / inf): finite, R in [0, 4], R exactly 1 at n.v <= 0 and 0 at n.l <= 0, R = 1 + (n.v / 0.1)
//           (unfaded twin - 1) for 0 < n.v < 0.1 (the design's 0.1, within 1e-4 max(R, 1)); sigma across each seam
//           within 0.02 absolute (s0_report 7.4) and R's jump there <= 0.03 per seam at u >= 0.2 (no blend: the
//           coordinator's seam rule after pdr2_'s design re-check; the worst record printed); WARP against the twin
//           within
//           1e-4 max(|twin|, 1) (the same code: every record and output, the worst record printed). WARP's own finite
//           check cannot see a NaN inside the lobes (GPU min / max return the other operand, so clamp makes it 0 or 4):
//           the twin's finite check (std::clamp keeps NaN) and the 1e-4 bar are the proof.
//   fast    VmfLobeFast against VmfLobe at base 0.5 (design 2.3's rule): weighted rms <= 3 %, max <= 10 % (u >= 0.2)
//   static  fxc and dxc (cs_6_5 and cs_6_9) without warnings; no indexable temp / immediate constant buffer (fxc), no
//           alloca, wave op or half (dxc)
// Controls (each must FAIL its gate; CONTROL lines): the preprint's series without x2 (T0w; WARP against that twin),
// the paper's Section 5 remap and beta 0 (T2's bond albedo; WARP against those twins), no fade (T4's n.v <= 0 rule;
// WARP against that twin), every beta + 0.001 (WARP against that twin: the bar sees a one-digit table slip), slider
// 50 % (T3), sigma(-u_v) - 0.001 (T2's denominator; the design's R2.2 c).
// tests.exe <repository> <folder of dxcompiler.dll and dxil.dll>; exit 0 when every gate passes and every control
// fails.
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <list>
#include <numbers>
#include <string>
#include <vector>

#include "../../src/games/twattila/vmf_diffuse.h"
#include "s0_points.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr uint32_t ONE_BITS = 0x3F800000u;
constexpr uint32_t GROUP = 64;
constexpr double SAME_CODE = 1e-4;  // |WARP - twin| <= SAME_CODE max(|twin|, 1)
constexpr double DEGREE = std::numbers::pi / 180.0;
// The design's fade cosine (vmf_diffuse.md 2 row 8), not the header's: a slip there moves the twin too.
constexpr float DESIGN_FADE_COSINE = 0.1f;

// The header's twin. Erf: Abramowitz and Stegun 7.1.26, odd (HLSL sign(0) = 0).
float Erf(float x) {
  const float t = 1.f / (1.f + (0.3275911f * std::abs(x)));
  const float poly =
      t * (0.254829592f + (t * (-0.284496736f + (t * (1.421413741f + (t * (-1.453152027f + (t * 1.061405429f))))))));
  const float sign = (x > 0.f) ? 1.f : ((x < 0.f) ? -1.f : 0.f);
  return sign * (1.f - (poly * std::exp(-x * x)));
}

struct Angles {
  float u_l = 0;
  float u_v = 0;
  float p = 0;
  float t = 0;
  float sines = 0;
  float s = 0;  // sin_l sin_v cos(phi), clamped to the sines
};

Angles AnglesOf(float n_dot_l, float n_dot_v, float l_dot_v) {
  const float u_l = std::clamp(n_dot_l, VMF_MIN_COSINE, 1.f);
  const float u_v = std::clamp(n_dot_v, VMF_MIN_COSINE, 1.f);
  const float sines = std::sqrt((1.f - (u_l * u_l)) * (1.f - (u_v * u_v)));
  return {.u_l = u_l,
          .u_v = u_v,
          .p = u_l * u_v,
          .t = u_l + u_v,
          .sines = sines,
          .s = std::clamp(l_dot_v - (u_l * u_v), -sines, sines)};
}

// The cross section sigma(u) by the paper's alpha branches (Eqs. 4-6), as VmfLobe's float3.
float Sigma(float u, float alpha) {
  const float a2 = alpha * alpha;
  if (alpha < 0.25f) {
    const float decay = std::exp(-2.f / a2);
    const float tanh_term = (1.f - decay) / (1.f + decay);
    const float rest = std::max(1.f - (u * u), 1e-12f);
    const float width = alpha * std::sqrt(rest);
    return (0.25f * ((tanh_term * (u - (a2 * std::abs(u)))) + ((-a2 - 2.f) * u) + (u / tanh_term)))
           + (0.5f * ((u * (Erf(u / width) + 1.f)) + (width * std::exp(-u * u / (a2 * rest)) * 0.56418958f)));
  }
  if (alpha < 0.9f) {
    const float u2 = u * u;
    const float fall = (1.f - alpha) * (1.f - alpha) * (1.f - alpha) * (1.f - alpha);
    return 0.25f + ((0.4975f - (0.273731f * std::pow(alpha - 0.1f, 1.7f))) * u)
           + ((0.153918f - (0.139522f * std::pow(alpha - 0.1f, 1.2f))) * ((3.f * u2) - 1.f))
           - ((0.013762f + (((0.013922f * alpha) - 0.027553f) * alpha)) * (3.f - (30.f * u2) + (35.f * u2 * u2)))
           + (fall * ((0.048357f * (u2 * u2) * (u2 * u2)) - (0.033493f * std::cos(7.7f * u))));
  }
  const float coth_term = 1.f / std::tanh(2.f / a2);
  return ((5.f / 128.f) * ((3.f * u * u) - 1.f) * ((3.f * a2 * a2) - (6.f * a2 * coth_term) + 4.f))
         - (0.25f * u * (a2 - (2.f * coth_term))) + 0.25f;
}

// pi f_k per unit c^k (orders 1-3, the series' cosine terms x `series`: 2 corrected, 1 the preprint's control), fm's
// angular and r factor (Eq. 9 without Ck / Ca, the ramp over r 0.15-0.40 included) and Eq. 8's denominator;
// `back_shift` is added to sigma(-u_v) (0; the design's R2.2 c control plants a shift).
struct Orders {
  float single = 0;
  float second = 0;
  float third = 0;
  float fm_shape = 0;
  float denominator = 0;
};

Orders OrdersOf(const Angles& angles, float r, float series, float back_shift = 0.f) {
  const float alpha = std::max(-std::log(1.f - std::sqrt(r)), VMF_MIN_ALPHA);
  const float sigma_l = Sigma(angles.u_l, alpha);
  const float shadowing = sigma_l * Sigma(angles.u_v, alpha);
  const float denominator = (angles.u_l * (Sigma(-angles.u_v, alpha) + back_shift)) + (angles.u_v * sigma_l);
  const float p = angles.p;
  const float t = angles.t;
  const float s = angles.s;
  const float squares = (angles.u_l * angles.u_l) + (angles.u_v * angles.u_v);

  const float r2 = r * r;
  const float r3 = r2 * r;
  const float r4 = r3 * r;
  const float r25 = r2 * std::sqrt(r);
  const float c100 = 1.f + (((0.84f * r4) - (0.1f * r)) / ((9.f * r3) + 1.f));
  const float c101 = ((-9.47f * r3) + (20.4f * r2) + (0.0173f * r)) / ((7.46f * r) + 1.f);
  const float c102 = ((2.37f * r2) - (0.927f * r)) / (r2 + 1.24f);
  const float c103 = ((-1.54f * r2) - (0.11f * r)) / ((7.1f * r2) - (1.05f * r) + 1.f);
  const float c110 = ((0.54f * r) - (0.182f * r3)) / ((1.32f * r2) + 1.f);
  const float c111 = ((-0.375f * r3) + (0.62f * r2) - (0.097f * r)) / ((0.4f * r3) + 1.f);
  const float c112 = (-0.681f * r2) + (0.862f * r) + 0.283f;
  const float c120 = ((5.1f * r2) + (2.25f * r)) / ((32.4f * r2) + (9.8f * r) + 1.f);
  const float c121 = ((6.f * r3) - (4.32f * r)) / ((287.f * r3) + (9.7f * r) + 1.f);
  const float c200 = ((0.226f * r2) + (0.00056f * r)) / ((7.07f * r2) + 1.f);
  const float c201 = ((-12.04f * r3) + (4.57f * r2) - (0.268f * r)) / ((36.7f * r3) + 1.f);
  const float c202 = ((-0.97f * r3) + (2.52f * r2) + (0.418f * r)) / ((10.f * r2) + 1.f);
  const float c203 = ((2.65f * r3) - (2.25f * r2) + (0.068f * r)) / ((21.4f * r3) + 1.f);
  const float c204 = ((0.05f * r) - (4.22f * r3)) / ((43.1f * r3) + (17.6f * r2) + 1.f);
  const float c210 = ((-0.027f * r3) - (0.049f * r)) / ((3.36f * r2) + 1.f);
  const float c211 = ((-8.332f * r25) + (6.073f * r3) + (2.77f * r2)) / ((50.f * r4) + 1.f);
  const float c212 = ((-0.295f * r3) - (0.431f * r2)) / ((23.9f * r3) + 1.f);
  const float c300 = ((0.262f * r4) - (0.083f * r3)) / ((38.6f * r4) - (1.9f * r2) + 1.f);
  const float c301 = ((4.95f * r25) - (2.44f * r3) - (0.627f * r2)) / ((31.5f * r4) + 1.f);
  const float c302 = ((0.31f * r25) + (1.4f * r3) + (0.33f * r2)) / ((20.f * r3) + 1.f);
  const float c303 = ((1.77f * r25) - (4.06f * r3) - (0.74f * r2)) / ((215.f * r4 * r) + 1.f);
  const float c304 = (-1.026f * r3) / ((13.2f * r3) + (5.81f * r2) + 1.f);
  const float c310 = ((0.028f * r2) - (0.0132f * r3)) / ((-3.315f * r4) + (7.46f * r2) + 1.f);
  const float c311 = ((0.162f * r25) + (0.302f * r3) - (0.134f * r2)) / ((57.5f * r4 * std::sqrt(r)) + 1.f);
  const float c312 = ((0.5f * r25) - (0.207f * r3) - (0.119f * r2)) / ((18.7f * r3) + 1.f);

  const float single_cos = std::pow(shadowing, c112) / denominator * s * (c110 + (c111 * p));
  const float single_cos2 = ((2.f * s * s) - (angles.sines * angles.sines)) * (c120 + (c121 * angles.u_l))
                            * (c120 + (c121 * angles.u_v)) / t;
  const float second_cos = s * (c210 + (c211 * p) + (c212 * t));
  const float third_cos = s * (c310 + (c311 * p) + (c312 * t));
  const float fm_ramp = std::clamp((r - VMF_FM_START) / VMF_FM_RANGE, 0.f, 1.f);
  return {
      .single = (shadowing / denominator * (c100 + (c101 * p) + (c102 * p * p) + (c103 * squares)))
                + (series * (single_cos + single_cos2)),
      .second = (c200 + (c201 * p) + (c202 * t) + (c203 * p * p) + (c204 * squares) + (series * second_cos)) / t,
      .third = (c300 + (c301 * p) + (c302 * t) + (c303 * p * p) + (c304 * squares) + (series * third_cos)) / t,
      .fm_shape = (std::abs(std::tanh(r) - 0.09f) - (0.0579f / (p + 0.288f))) * fm_ramp,
      .denominator = denominator,
  };
}

// pi f at the microsurface albedo c (Eq. 7, Eq. 9's fm).
float PiF(const Orders& orders, float c, float r) {
  const float root = std::sqrt(1.f - c);
  const float ck = ((-0.453f * (1.f - c)) - (0.544f * root) + 1.f) / ((1.429f * root) + 1.f);
  const float ca = c * std::pow((1.169f * root) + 1.f, -std::atan((((r - 0.265f) * r) + 0.0225f) * Erf(root)));
  return (c * (orders.single + (c * (orders.second + (c * orders.third)))))
         + (VMF_PI * 0.384f * orders.fm_shape * ck * (ca - 0.342f) * ca);
}

// How c follows the base: S0's beta table (brightness kept), beta 0 (c = base, the physical form) or the paper's
// Section 5 remap (normal incidence); the last two are controls, as are beta_offset != 0 and fade off.
enum class AlbedoMatch : uint8_t { TABLE, NONE, REMAP };

struct TwinOptions {
  float series = 2.f;
  AlbedoMatch match = AlbedoMatch::TABLE;
  bool fade = true;
  float beta_offset = 0.f;
};

// The beta table at (r, albedo), bilinear as the header's separable sum of clamped ramps (rows, then columns).
float Beta(float r, float albedo) {
  std::array<float, 8> beta_row = {};
  for (size_t column = 0; column < 8; ++column) {
    beta_row[column] = VMF_BETA[0][column];
  }
  for (size_t row = 1; row < 8; ++row) {
    const float ramp = std::clamp((r - VMF_BETA_R[row - 1]) / (VMF_BETA_R[row] - VMF_BETA_R[row - 1]), 0.f, 1.f);
    for (size_t column = 0; column < 8; ++column) {
      beta_row[column] += ramp * (VMF_BETA[row][column] - VMF_BETA[row - 1][column]);
    }
  }
  float beta = beta_row[0];
  for (size_t node = 1; node < 8; ++node) {
    const float ramp =
        std::clamp((albedo - VMF_BETA_BASE[node - 1]) / (VMF_BETA_BASE[node] - VMF_BETA_BASE[node - 1]), 0.f, 1.f);
    beta += ramp * (beta_row[node] - beta_row[node - 1]);
  }
  return beta;
}

std::array<float, 3> TwinLobe(float n_dot_l, float n_dot_v, float l_dot_v, const std::array<float, 3>& base, float r,
                              const TwinOptions& options) {
  if (r <= 0.f) return {1.f, 1.f, 1.f};
  if (n_dot_l <= 0.f) return {0.f, 0.f, 0.f};
  r = std::min(r, VMF_MAX_ROUGHNESS);
  const Orders orders = OrdersOf(AnglesOf(n_dot_l, n_dot_v, l_dot_v), r, options.series);
  const float fade = options.fade ? std::clamp(n_dot_v / VMF_FADE_COSINE, 0.f, 1.f) : 1.f;
  std::array<float, 3> lobe = {};
  for (size_t channel = 0; channel < 3; ++channel) {
    const float albedo = std::clamp(base[channel], VMF_MIN_ALBEDO, VMF_MAX_ALBEDO);
    float c = albedo;
    switch (options.match) {
      case AlbedoMatch::TABLE: {
        const float beta = Beta(r, albedo) + options.beta_offset;
        c = std::min(albedo / ((1.f - beta) + (beta * albedo)), VMF_MAX_ALBEDO);
        break;
      }
      case AlbedoMatch::REMAP: {
        const float s = 0.64985f + (0.631112f * r) + (1.38922f * r * r);
        const float k = albedo;
        const float fit = (-1.f + k + std::sqrt(1.f - (2.f * k) + (k * k) + (4.f * s * s * k * k))) / (2.f * s * k);
        c = std::min((fit * std::sqrt(r)) + ((1.f - std::sqrt(r)) * k), VMF_MAX_ALBEDO);
        break;
      }
      case AlbedoMatch::NONE:
        break;
    }
    const float ratio = std::clamp(PiF(orders, c, r) / albedo, 0.f, VMF_LOBE_MAX);
    lobe[channel] = 1.f + (fade * (ratio - 1.f));
  }
  return lobe;
}

float TwinLobeFast(float n_dot_l, float n_dot_v, float l_dot_v, float r) {
  if (r <= 0.f) return 1.f;
  if (n_dot_l <= 0.f) return 0.f;
  r = std::min(r, VMF_MAX_ROUGHNESS);
  const Angles angles = AnglesOf(n_dot_l, n_dot_v, l_dot_v);
  const std::array<float, 7> features = {
      1.f, angles.t, angles.s, angles.s / angles.t, angles.s * angles.s / angles.t,
      angles.sines * angles.sines / angles.t, angles.s / (angles.p + 0.3f)};
  float lobe = 1.f;
  for (size_t k = 0; k < 7; ++k) {
    lobe += r * (VMF_FAST[k][0] + (r * (VMF_FAST[k][1] + (r * VMF_FAST[k][2])))) * features[k];
  }
  const float fade = std::clamp(n_dot_v / VMF_FADE_COSINE, 0.f, 1.f);
  return 1.f + (fade * (std::clamp(lobe, VMF_FAST_MIN, VMF_LOBE_MAX) - 1.f));
}

// l.v of two directions with cosines u_l, u_v to the normal and azimuths phi degrees apart.
float LDotV(float u_l, float u_v, double phi_degrees) {
  const float sines = std::sqrt((1.f - (u_l * u_l)) * (1.f - (u_v * u_v)));
  return (u_l * u_v) + (sines * static_cast<float>(std::cos(phi_degrees * DEGREE)));
}

// vmf_probe.hlsl's records.
struct ProbeInput {
  float n_dot_l = 0;
  float n_dot_v = 0;
  float l_dot_v = 0;
  float r = 0;
  std::array<float, 3> base = {};
  float rho = 0;
  float slider = 0;
  float erf_x = 0;
};
static_assert(sizeof(ProbeInput) == 40);

struct ProbeOutput {
  std::array<float, 3> lobe = {};
  float fast = 0;
  float roughness = 0;
  std::array<float, 3> lobe_at_roughness = {};
  float erf = 0;
};
static_assert(sizeof(ProbeOutput) == 36);

enum class RecordKind : uint8_t { T3, SWEEP, SEAM, GRID, PAD };

// fxc.exe's include rule (the add-ons' build): a nested include resolves against the including file's folder
// (D3D_COMPILE_STANDARD_FILE_INCLUDE takes the first file's folder at every level).
class IncludeBesideParent : public ID3DInclude {
 public:
  explicit IncludeBesideParent(const std::filesystem::path& folder) : folder_(folder) {}

  HRESULT __stdcall Open(D3D_INCLUDE_TYPE /*type*/, LPCSTR name, LPCVOID parent, LPCVOID* data,
                         UINT* size) noexcept override {
    std::filesystem::path folder = folder_;
    for (const File& file : files_) {
      if (file.text.data() == parent) folder = file.folder;
    }
    const std::filesystem::path path = (folder / name).lexically_normal();
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return E_FAIL;
    const File& file = files_.emplace_back(File{
        .folder = path.parent_path(), .text = std::string(std::istreambuf_iterator<char>(stream), {})});
    *data = file.text.data();
    *size = static_cast<UINT>(file.text.size());
    return S_OK;
  }

  HRESULT __stdcall Close(LPCVOID /*data*/) noexcept override { return S_OK; }

 private:
  struct File {
    std::filesystem::path folder;
    std::string text;
  };
  std::filesystem::path folder_;
  std::list<File> files_;  // stable addresses: fxc hands a file's text back as the next include's parent
};

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::printf("usage: tests.exe <repository> <folder of dxcompiler.dll and dxil.dll>\n");
    return 2;
  }
  const std::string repository = argv[1];
  const std::string dxc_folder = argv[2];
  int gates_failed = 0;
  int controls_passed = 0;
  const auto gate = [&](const std::string& name, bool passed, const std::string& detail) {
    std::printf("%s %s: %s\n", passed ? "PASS" : "FAIL", name.c_str(), detail.c_str());
    if (!passed) ++gates_failed;
  };
  // A control must fail its gate.
  const auto control = [&](const std::string& name, bool gate_failed, const std::string& detail) {
    std::printf("CONTROL %s %s: %s\n", gate_failed ? "fails as required" : "PASSES (wrong)", name.c_str(),
                detail.c_str());
    if (!gate_failed) ++controls_passed;
  };
  // A setup step that failed: the run stops there, saying which.
  const auto setup_failed = [](const char* step) {
    std::printf("FAIL setup: %s\n", step);
    return 1;
  };
  const auto format = [](const char* pattern, auto... values) {
    std::array<char, 2048> text = {};
    std::snprintf(text.data(), text.size(), pattern, values...);
    return std::string(text.data());
  };

  // erf.
  {
    double worst = 0;
    for (int step = -6000; step <= 6000; ++step) {
      const float x = static_cast<float>(step) * 1e-3f;
      worst = std::max(worst, std::abs(static_cast<double>(Erf(x)) - std::erf(static_cast<double>(x))));
    }
    gate("erf (A-S 7.1.26, float) vs std::erf on [-6, 6]", worst <= 1e-6, format("max |error| %.2e (<= 1e-6)", worst));
  }

  // T0p / T0w and the series control.
  {
    double port_worst = 0;
    std::string walk_cells;
    std::string printed_cells;
    bool walk_ok = true;
    // Every cell at r >= 0.05 (the preprint's series passes at r <= 0.02: its lobe is too small).
    bool printed_fails_everywhere = true;
    size_t first = 0;
    while (first < std::size(T0_POINTS)) {
      size_t last = first;
      double walk_squares = 0;
      double printed_squares = 0;
      while (last < std::size(T0_POINTS) && T0_POINTS[last].r == T0_POINTS[first].r
             && T0_POINTS[last].c == T0_POINTS[first].c) {
        const T0Point& point = T0_POINTS[last];
        const float u_l = static_cast<float>(std::cos(point.theta_in * DEGREE));
        const float u_v = static_cast<float>(std::cos(point.theta_out * DEGREE));
        const double l_dot_v = (std::cos(point.theta_in * DEGREE) * std::cos(point.theta_out * DEGREE))
                               + (std::sin(point.theta_in * DEGREE) * std::sin(point.theta_out * DEGREE)
                                  * std::cos(point.phi_out * DEGREE));
        const Angles angles = AnglesOf(u_l, u_v, static_cast<float>(l_dot_v));
        const float r = static_cast<float>(point.r);
        const float c = static_cast<float>(point.c);
        const double f = PiF(OrdersOf(angles, r, 2.f), c, r) / VMF_PI;
        const double printed = PiF(OrdersOf(angles, r, 1.f), c, r) / VMF_PI;
        port_worst = std::max(port_worst, std::abs((f / point.model) - 1.0));
        walk_squares += ((f / point.walk) - 1.0) * ((f / point.walk) - 1.0);
        printed_squares += ((printed / point.walk) - 1.0) * ((printed / point.walk) - 1.0);
        ++last;
      }
      const double walk_rms = std::sqrt(walk_squares / static_cast<double>(last - first));
      const double printed_rms = std::sqrt(printed_squares / static_cast<double>(last - first));
      walk_ok = walk_ok && walk_rms <= 0.03;
      if (T0_POINTS[first].r >= 0.05) printed_fails_everywhere = printed_fails_everywhere && printed_rms > 0.03;
      walk_cells += format(" r%.3f/c%.1f %.2f%%", T0_POINTS[first].r, T0_POINTS[first].c, 100 * walk_rms);
      printed_cells += format(" r%.3f/c%.1f %.1f%%", T0_POINTS[first].r, T0_POINTS[first].c, 100 * printed_rms);
      first = last;
    }
    gate("T0p twin vs S0's double model (2040 points)", port_worst <= 1e-4,
         format("max relative %.2e (<= 1e-4)", port_worst));
    gate("T0w twin vs S0's walk, rms per (r, c) <= 3 %", walk_ok, walk_cells);
    control("T0w with the preprint's series (no x2), every cell r >= 0.05 over 3 %", printed_fails_everywhere,
            printed_cells);
  }

  // T2: reciprocity and range on a grid, the denominator, the bond albedo with the table and its controls.
  {
    double worst = 0;
    float lowest = 1;
    float highest = 1;
    const std::array<std::array<float, 3>, 2> bases = {{{0.05f, 0.3f, 0.6f}, {0.9f, 0.999f, 1.5f}}};
    for (const float r : {0.005f, 0.0489f, 0.05f, 0.155f, 0.26f, 0.352f, 0.40f}) {
      for (const std::array<float, 3>& base : bases) {
        for (int i = 2; i <= 20; ++i) {
          for (int j = 2; j <= 20; ++j) {
            for (int step = 0; step <= 12; ++step) {
              const float u_l = static_cast<float>(i) * 0.05f;
              const float u_v = static_cast<float>(j) * 0.05f;
              const float l_dot_v = LDotV(u_l, u_v, 15.0 * step);
              const std::array<float, 3> forward = TwinLobe(u_l, u_v, l_dot_v, base, r, {});
              const std::array<float, 3> backward = TwinLobe(u_v, u_l, l_dot_v, base, r, {});
              for (size_t channel = 0; channel < 3; ++channel) {
                const float scale = std::max({forward[channel], backward[channel], 1.f});
                worst = std::max(worst, static_cast<double>(std::abs(forward[channel] - backward[channel]) / scale));
                lowest = std::min(lowest, forward[channel]);
                highest = std::max(highest, forward[channel]);
              }
            }
          }
        }
      }
    }
    gate("T2 reciprocity (twin, u >= 0.1)", worst <= 1e-5,
         format("max |R(l,v) - R(v,l)| / max(R, 1) %.2e (<= 1e-5)", worst));
    gate("T2 R in [0, 4] (twin, u >= 0.1)", lowest >= 0.f && highest <= 4.f,
         format("R in [%.4f, %.4f]", lowest, highest));

    // Eq. 8's denominator over u in [1e-4, 1]^2 (log-spaced below 0.05) and r in (0, 0.40] (log-spaced, the seams
    // and their neighbours included): the header's comment states >= 0.5 u_l u_v (measured 0.62; 0.5 keeps a margin).
    std::vector<float> cosines;
    for (int step = 0; step <= 40; ++step) {
      cosines.push_back(static_cast<float>(1e-4 * std::pow(500.0, step / 40.0)));
    }
    for (int step = 1; step <= 95; ++step) {
      cosines.push_back(0.05f + (static_cast<float>(step) * 0.01f));
    }
    std::vector<float> roughnesses = {0.0489290f, 0.0489291f, 0.352159f, 0.352160f, 0.40f};
    for (int step = 0; step <= 400; ++step) {
      roughnesses.push_back(static_cast<float>(1e-8 * std::pow(0.4e8, step / 400.0)));
    }
    // min(den / (u_l u_v)) over the sweep with sigma(-u_v) shifted by `back_shift`, and where.
    const auto lowest_denominator = [&](float back_shift, std::string* where) {
      double lowest = std::numeric_limits<double>::infinity();
      for (const float r : roughnesses) {
        for (const float u_l : cosines) {
          for (const float u_v : cosines) {
            const double ratio =
                OrdersOf(AnglesOf(u_l, u_v, u_l * u_v), r, 2.f, back_shift).denominator / (u_l * u_v);
            if (ratio < lowest) {
              lowest = ratio;
              *where = format("r %.4g, u_l %.4g, u_v %.4g", r, u_l, u_v);
            }
          }
        }
      }
      return lowest;
    };
    std::string lowest_where;
    const double lowest_ratio = lowest_denominator(0.f, &lowest_where);
    gate("T2 denominator u_l sigma(-u_v) + u_v sigma(u_l) >= 0.5 u_l u_v", lowest_ratio >= 0.5,
         format("min ratio %.4f (>= 0.5) at %s", lowest_ratio, lowest_where.c_str()));
    // The design's R2.2 c control: sigma(-u_v) - 0.001 (a slip of one digit in the unclamped back term).
    const double shifted_ratio = lowest_denominator(-0.001f, &lowest_where);
    control("T2 denominator with sigma(-u_v) - 0.001", shifted_ratio < 0.5,
            format("min ratio %.4f at %s", shifted_ratio, lowest_where.c_str()));

    // bond / base = 2 Int u_l du_l Int u_v du_v (1 / pi) Int R dphi: Gauss-Legendre in u (24), trapezoid in phi over
    // [0, pi] (25, the even integrand).
    std::array<double, 24> nodes = {};
    std::array<double, 24> weights = {};
    const double count = static_cast<double>(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
      double z = std::cos(std::numbers::pi * (static_cast<double>(i) + 0.75) / (count + 0.5));
      double derivative = 1;
      for (int iteration = 0; iteration < 100; ++iteration) {
        double p1 = 1;
        double p2 = 0;
        for (size_t order = 1; order <= nodes.size(); ++order) {
          const double p3 = p2;
          const double n = static_cast<double>(order);
          p2 = p1;
          p1 = ((((2.0 * n) - 1.0) * z * p2) - ((n - 1.0) * p3)) / n;
        }
        derivative = count * ((z * p1) - p2) / ((z * z) - 1.0);
        const double previous = z;
        z = previous - (p1 / derivative);
        if (std::abs(z - previous) < 1e-15) break;
      }
      nodes[i] = 0.5 * (1.0 - z);
      weights[i] = 1.0 / ((1.0 - (z * z)) * derivative * derivative);
    }
    const auto bond = [&](float r, float base, const TwinOptions& options) {
      double sum = 0;
      for (size_t i = 0; i < nodes.size(); ++i) {
        for (size_t j = 0; j < nodes.size(); ++j) {
          const float u_l = static_cast<float>(nodes[i]);
          const float u_v = static_cast<float>(nodes[j]);
          double phi_sum = 0;
          for (int step = 0; step <= 24; ++step) {
            const double end_weight = (step == 0 || step == 24) ? 0.5 : 1.0;
            phi_sum += end_weight * TwinLobe(u_l, u_v, LDotV(u_l, u_v, 7.5 * step), {base, base, base}, r, options)[0];
          }
          sum += weights[i] * weights[j] * nodes[i] * nodes[j] * (2.0 / 24.0) * phi_sum;
        }
      }
      return 2.0 * sum;
    };
    double table_worst = 0;
    double unfaded_worst = 0;
    double remap_worst = 0;
    double none_worst = 0;
    std::string table_rows;
    for (const float r : {0.005f, 0.02f, 0.05f, 0.08f, 0.12f, 0.155f, 0.2f, 0.26f, 0.33f, 0.40f}) {
      table_rows += format(" r%.3f:", r);
      for (const float base : {0.05f, 0.1f, 0.2f, 0.3f, 0.5f, 0.65f, 0.8f, 0.95f}) {
        const double table = bond(r, base, {}) - 1.0;
        table_worst = std::max(table_worst, std::abs(table));
        unfaded_worst = std::max(unfaded_worst, std::abs(bond(r, base, {.fade = false}) - 1.0));
        remap_worst = std::max(remap_worst, std::abs(bond(r, base, {.match = AlbedoMatch::REMAP}) - 1.0));
        none_worst = std::max(none_worst, std::abs(bond(r, base, {.match = AlbedoMatch::NONE}) - 1.0));
        table_rows += format(" %+.2f", 100 * table);
      }
    }
    double beta_worst = 0;
    std::string beta_where;
    for (const BetaPoint& point : BETA_POINTS) {
      const double gap = std::abs(Beta(static_cast<float>(point.r), static_cast<float>(point.base)) - point.beta);
      if (gap > beta_worst) {
        beta_worst = gap;
        beta_where = format("r %.3f, base %.2f", point.r, point.base);
      }
    }
    gate("T2 vmf_diffuse.h's beta table at S0's 80 check points (beta_check.csv)", beta_worst <= 2e-6,
         format("max |beta - S0| %.2e (<= 2e-6) at %s", beta_worst, beta_where.c_str()));
    gate("T2 bond albedo with the beta table within 1 % (base x R as shipped: clamp and fade)", table_worst <= 0.01,
         format("max |bond / base - 1| %.2f%% (without the fade %.2f%%); per r, base 0.05 .. 0.95 in %%:%s",
                100 * table_worst, 100 * unfaded_worst, table_rows.c_str()));
    control("T2 bond albedo with the paper's Section 5 remap", remap_worst > 0.01,
            format("max |bond / base - 1| %.1f%%", 100 * remap_worst));
    control("T2 bond albedo with beta 0 (c = base)", none_worst > 0.01,
            format("max |bond / base - 1| %.1f%%", 100 * none_worst));
  }

  // fast: the fast form against tier 1 at base 0.5, S0's grid.
  {
    bool fast_ok = true;
    std::string rows;
    const std::array<float, 14> grid_r = {0.005f, 0.01f, 0.02f, 0.035f, 0.05f, 0.075f, 0.1f,
                                          0.125f, 0.155f, 0.2f,  0.25f,  0.3f,   0.35f, 0.40f};
    for (const float r : grid_r) {
      double squares = 0;
      double weight_sum = 0;
      double worst = 0;
      for (int i = 0; i < 24; ++i) {
        for (int j = 0; j < 24; ++j) {
          for (int step = 0; step <= 12; ++step) {
            const float u_l = (static_cast<float>(i) + 0.5f) / 24.f;
            const float u_v = (static_cast<float>(j) + 0.5f) / 24.f;
            const float l_dot_v = LDotV(u_l, u_v, 15.0 * step);
            const float tier1 = TwinLobe(u_l, u_v, l_dot_v, {0.5f, 0.5f, 0.5f}, r, {})[0];
            const double error = (TwinLobeFast(u_l, u_v, l_dot_v, r) - tier1) / std::max(tier1, 0.25f);
            squares += u_l * u_v * error * error;
            weight_sum += u_l * u_v;
            if (u_l >= 0.2f && u_v >= 0.2f) worst = std::max(worst, std::abs(error));
          }
        }
      }
      const double rms = std::sqrt(squares / weight_sum);
      fast_ok = fast_ok && rms <= 0.03 && worst <= 0.10;
      rows += format(" r%.3f %.2f/%.1f%%", r, 100 * rms, 100 * worst);
    }
    gate("fast VmfLobeFast vs VmfLobe (base 0.5), weighted rms <= 3 %, max <= 10 % (u >= 0.2)", fast_ok, rows);
  }

  // T4 sigma at the seams (s0_report 1 and 7.4: absolute 0.02): just under and at alpha 0.25 (Eq. 4 / 5) and 0.9
  // (Eq. 5 / 6), u in [-1, 1].
  {
    std::string rows;
    bool seams_ok = true;
    for (const float seam_alpha : {0.25f, 0.9f}) {
      double worst = 0;
      for (int step = -100; step <= 100; ++step) {
        const float u = static_cast<float>(step) / 100.f;
        const float jump = Sigma(u, seam_alpha * (1.f - 1e-6f)) - Sigma(u, seam_alpha);
        worst = std::max(worst, static_cast<double>(std::abs(jump)));
      }
      seams_ok = seams_ok && worst <= 0.02;
      rows += format(" alpha %.2f %.4f", seam_alpha, worst);
    }
    gate("T4 sigma across the Eq. 4 / 5 and 5 / 6 seams, absolute <= 0.02", seams_ok,
         format("max jump:%s", rows.c_str()));
  }

  // The records: T3 (r 0; slider 0, rho 0, and the slider 50 % control), the T4 sweep (the fade's ramp included), the
  // seams, the grid.
  std::vector<ProbeInput> inputs;
  std::vector<RecordKind> kinds;
  const std::array<float, 9> sweep_cosines = {-0.3f, 0.f, 1e-8f, 1e-4f, 0.03f, 0.05f, 0.08f, 0.1f, 1.f};
  const std::array<float, 5> sweep_bases = {0.f, 1e-5f, 0.999f, 1.f, 1.5f};
  const float infinity = std::numeric_limits<float>::infinity();
  const std::array<float, 10> erf_inputs = {0.f, 1.f, -1.f, 1e-3f, 0.5f, 3.f, 10.f, 1e10f, infinity, -infinity};
  // (rho, slider); the last is the control.
  const std::array<std::array<float, 2>, 4> rho_sliders = {{{1.f, 0.f}, {0.f, 1.f}, {0.7f, 0.f}, {1.f, 0.5f}}};
  const auto add = [&](RecordKind kind, const ProbeInput& input) {
    inputs.push_back(input);
    kinds.push_back(kind);
  };
  size_t bases_used = 0;
  // Every sweep base in every channel in turn.
  const auto sweep_base = [&]() {
    const std::array<float, 3> base = {sweep_bases[bases_used % 5], sweep_bases[(bases_used + 1) % 5],
                                       sweep_bases[(bases_used + 3) % 5]};
    ++bases_used;
    return base;
  };
  const auto next_erf = [&]() { return erf_inputs[inputs.size() % erf_inputs.size()]; };
  for (const float n_dot_l : sweep_cosines) {
    for (const float n_dot_v : sweep_cosines) {
      for (const float l_dot_v : {-1.f, 0.f, 1.f}) {
        for (const std::array<float, 2>& rho_slider : rho_sliders) {
          add(RecordKind::T3, {.n_dot_l = n_dot_l, .n_dot_v = n_dot_v, .l_dot_v = l_dot_v, .r = 0.f,
                               .base = sweep_base(), .rho = rho_slider[0], .slider = rho_slider[1],
                               .erf_x = next_erf()});
        }
      }
    }
  }
  for (const float u : {0.3f, 0.7f, 1.f}) {
    for (const std::array<float, 2>& rho_slider : rho_sliders) {
      add(RecordKind::T3, {.n_dot_l = u, .n_dot_v = 0.8f, .l_dot_v = 0.5f, .r = 0.f, .base = {0.2f, 0.5f, 0.8f},
                           .rho = rho_slider[0], .slider = rho_slider[1], .erf_x = 0.f});
    }
  }
  const float seam_45 = static_cast<float>(std::pow(1.0 - std::exp(-0.25), 2.0));  // alpha 0.25: r 0.048929
  const float seam_56 = static_cast<float>(std::pow(1.0 - std::exp(-0.9), 2.0));   // alpha 0.9: r 0.352160
  for (const float r : {1e-6f, seam_45, seam_56, 0.40f, 1e-12f, 1e-30f}) {
    for (const float n_dot_l : sweep_cosines) {
      for (const float n_dot_v : sweep_cosines) {
        for (const float l_dot_v : {-1.f, 1.f}) {
          add(RecordKind::SWEEP, {.n_dot_l = n_dot_l, .n_dot_v = n_dot_v, .l_dot_v = l_dot_v, .r = r,
                                  .base = sweep_base(), .rho = 1.f, .slider = 0.5f, .erf_x = next_erf()});
        }
      }
    }
  }
  // The fade's ramp on consistent directions: n.v 0.03 / 0.05 / 0.08 against every n.l of the grid.
  for (const float r : {0.05f, 0.155f, 0.40f}) {
    for (const float n_dot_v : {0.03f, 0.05f, 0.08f}) {
      for (int i = 1; i <= 10; ++i) {
        for (int step = 0; step <= 6; ++step) {
          const float n_dot_l = static_cast<float>(i) * 0.1f;
          add(RecordKind::SWEEP, {.n_dot_l = n_dot_l, .n_dot_v = n_dot_v,
                                  .l_dot_v = LDotV(n_dot_l, n_dot_v, 30.0 * step),
                                  .r = r, .base = {0.05f, 0.5f, 0.95f}, .rho = 0.9f, .slider = 1.f, .erf_x = 0.25f});
        }
      }
    }
  }
  const size_t seam_start = inputs.size();
  for (const float seam : {seam_45, seam_56}) {
    for (int i = 1; i <= 10; ++i) {
      for (int j = 1; j <= 10; ++j) {
        for (int step = 0; step <= 6; ++step) {
          const float u_l = static_cast<float>(i) * 0.1f;
          const float u_v = static_cast<float>(j) * 0.1f;
          for (const float side : {1.f - 1e-4f, 1.f + 1e-4f}) {
            add(RecordKind::SEAM, {.n_dot_l = u_l, .n_dot_v = u_v, .l_dot_v = LDotV(u_l, u_v, 30.0 * step),
                                   .r = seam * side, .base = {0.05f, 0.5f, 0.95f}, .rho = 0.85f, .slider = 1.f,
                                   .erf_x = u_l - u_v});
          }
        }
      }
    }
  }
  const std::array<float, 9> grid_r = {0.005f, 0.02f, 0.05f, 0.1f, 0.155f, 0.2f, 0.26f, 0.33f, 0.40f};
  const std::array<std::array<float, 3>, 2> grid_bases = {{{0.2f, 0.5f, 0.8f}, {0.05f, 0.65f, 0.95f}}};
  const size_t grid_start = inputs.size();
  // The grid's record of (r, base, u_l, u_v, phi); its reciprocal swaps u_l and u_v.
  const auto grid_index = [&](size_t r_index, size_t base_index, size_t i, size_t j, size_t step) {
    return grid_start + ((((((((r_index * 2) + base_index) * 10) + i) * 10) + j) * 7) + step);
  };
  for (size_t r_index = 0; r_index < grid_r.size(); ++r_index) {
    for (size_t base_index = 0; base_index < 2; ++base_index) {
      for (size_t i = 0; i < 10; ++i) {
        for (size_t j = 0; j < 10; ++j) {
          for (size_t step = 0; step < 7; ++step) {
            const float u_l = static_cast<float>(i + 1) * 0.1f;
            const float u_v = static_cast<float>(j + 1) * 0.1f;
            add(RecordKind::GRID, {.n_dot_l = u_l, .n_dot_v = u_v,
                                   .l_dot_v = LDotV(u_l, u_v, 30.0 * static_cast<double>(step)),
                                   .r = grid_r[r_index], .base = grid_bases[base_index],
                                   .rho = static_cast<float>(step) / 6.f, .slider = static_cast<float>(i) / 9.f,
                                   .erf_x = (u_l - u_v) * 4.f});
          }
        }
      }
    }
  }
  while (inputs.size() % GROUP != 0) {
    add(RecordKind::PAD, {.n_dot_l = 0.5f, .n_dot_v = 0.5f, .l_dot_v = 0.25f, .r = 0.1f, .base = {0.5f, 0.5f, 0.5f}});
  }
  std::printf("records: %zu (T3, sweep, seams from %zu, grid from %zu)\n", inputs.size(), seam_start, grid_start);

  const auto twin_outputs = [&](const TwinOptions& options) {
    std::vector<ProbeOutput> outputs;
    outputs.reserve(inputs.size());
    for (const ProbeInput& input : inputs) {
      const float edge = 1.f - std::exp(-input.rho * input.rho);
      const float roughness = input.slider * edge * edge;
      outputs.push_back({
          .lobe = TwinLobe(input.n_dot_l, input.n_dot_v, input.l_dot_v, input.base, input.r, options),
          .fast = TwinLobeFast(input.n_dot_l, input.n_dot_v, input.l_dot_v, input.r),
          .roughness = roughness,
          .lobe_at_roughness = TwinLobe(input.n_dot_l, input.n_dot_v, input.l_dot_v, input.base, roughness, options),
          .erf = Erf(input.erf_x),
      });
    }
    return outputs;
  };
  const std::vector<ProbeOutput> twin = twin_outputs({});
  const std::vector<ProbeOutput> unfaded = twin_outputs({.fade = false});

  // T3 / T4 / the fade / seams / reciprocity / erf on one build's outputs (the twin's or a WARP build's).
  const auto check_build = [&](const std::string& build, const std::vector<ProbeOutput>& outputs) {
    double fade_worst = 0;
    bool t3_ok = true;
    bool control_all_one = true;
    bool finite = true;
    bool in_range = true;
    bool back_facing_one = true;
    bool below_zero = true;
    // Per seam (Eq. 4 / 5, Eq. 5 / 6): the worst R jump at u >= 0.2 and its record; u >= 0.1 printed beside it.
    std::array<double, 2> seam_worst = {};
    std::array<double, 2> seam_wide = {};
    std::array<std::string, 2> seam_where;
    double reciprocity_worst = 0;
    double erf_worst = 0;
    bool erf_ends = true;
    for (size_t index = 0; index < inputs.size(); ++index) {
      const ProbeInput& input = inputs[index];
      const ProbeOutput& output = outputs[index];
      const std::array<float, 4> lobes = {output.lobe[0], output.lobe[1], output.lobe[2], output.fast};
      const std::array<float, 9> values = {output.lobe[0], output.lobe[1], output.lobe[2], output.fast,
                                           output.roughness, output.lobe_at_roughness[0], output.lobe_at_roughness[1],
                                           output.lobe_at_roughness[2], output.erf};
      finite = finite && std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
      if (kinds[index] == RecordKind::T3) {
        for (const float value : lobes) {
          t3_ok = t3_ok && std::bit_cast<uint32_t>(value) == ONE_BITS;
        }
        const bool lambert = input.slider == 0.f || input.rho == 0.f;
        for (const float value : output.lobe_at_roughness) {
          if (lambert) {
            t3_ok = t3_ok && std::bit_cast<uint32_t>(value) == ONE_BITS;
          } else if (input.n_dot_l > 0.f && input.n_dot_v >= 0.1f) {
            control_all_one = control_all_one && std::bit_cast<uint32_t>(value) == ONE_BITS;
          }
        }
      }
      if (kinds[index] == RecordKind::SWEEP || kinds[index] == RecordKind::SEAM || kinds[index] == RecordKind::GRID) {
        for (const float value : lobes) {
          in_range = in_range && value >= 0.f && value <= 4.f;
        }
      }
      if (kinds[index] == RecordKind::SWEEP && input.n_dot_l > 0.f && input.n_dot_v <= 0.f) {
        for (const float value : lobes) {
          back_facing_one = back_facing_one && std::bit_cast<uint32_t>(value) == ONE_BITS;
        }
      }
      if (kinds[index] == RecordKind::SWEEP && input.n_dot_l > 0.f && input.n_dot_v > 0.f
          && input.n_dot_v < DESIGN_FADE_COSINE) {
        const float weight = input.n_dot_v / DESIGN_FADE_COSINE;
        for (size_t channel = 0; channel < 3; ++channel) {
          const double expected = 1.0 + (weight * (unfaded[index].lobe[channel] - 1.0));
          fade_worst = std::max(fade_worst, std::abs(output.lobe[channel] - expected) / std::max(expected, 1.0));
        }
      }
      if (kinds[index] == RecordKind::SWEEP && input.n_dot_l <= 0.f) {
        for (const float value : lobes) {
          below_zero = below_zero && value == 0.f;
        }
      }
      if (kinds[index] == RecordKind::SEAM && ((index - seam_start) % 2) == 1) {
        const size_t seam = (input.r < 0.2f) ? 0 : 1;
        for (size_t channel = 0; channel < 3; ++channel) {
          const float below = outputs[index - 1].lobe[channel];
          const double jump = std::abs(output.lobe[channel] - below);
          seam_wide[seam] = std::max(seam_wide[seam], jump);
          if (input.n_dot_l >= 0.2f && input.n_dot_v >= 0.2f && jump > seam_worst[seam]) {
            seam_worst[seam] = jump;
            seam_where[seam] = format("u_l %.1f, u_v %.1f, l.v %+.3f, base %.2f: %.3f / %.3f", input.n_dot_l,
                                      input.n_dot_v, input.l_dot_v, input.base[channel], below, output.lobe[channel]);
          }
        }
      }
      if (std::isfinite(input.erf_x)) {
        const double exact = std::erf(static_cast<double>(input.erf_x));
        erf_worst = std::max(erf_worst, std::abs(static_cast<double>(output.erf) - exact));
      } else {
        erf_ends = erf_ends && output.erf == ((input.erf_x > 0.f) ? 1.f : -1.f);
      }
      if (input.erf_x == 0.f) erf_ends = erf_ends && output.erf == 0.f;
    }
    for (size_t r_index = 0; r_index < grid_r.size(); ++r_index) {
      for (size_t base_index = 0; base_index < 2; ++base_index) {
        for (size_t i = 0; i < 10; ++i) {
          for (size_t j = 0; j < 10; ++j) {
            for (size_t step = 0; step < 7; ++step) {
              const ProbeOutput& forward = outputs[grid_index(r_index, base_index, i, j, step)];
              const ProbeOutput& backward = outputs[grid_index(r_index, base_index, j, i, step)];
              for (size_t channel = 0; channel < 3; ++channel) {
                const float scale = std::max({forward.lobe[channel], backward.lobe[channel], 1.f});
                const double gap = std::abs(forward.lobe[channel] - backward.lobe[channel]) / scale;
                reciprocity_worst = std::max(reciprocity_worst, gap);
              }
            }
          }
        }
      }
    }
    gate(build + " T3 r 0, slider 0, rho 0: exactly 1.0f", t3_ok,
         "VmfLobe, VmfLobeFast, VmfLobe(VmfRoughness(rho, slider))");
    control(build + " T3 with slider 50 % (rho 1)", !control_all_one,
            control_all_one ? "all exactly 1" : "R != 1 where n.l > 0, n.v >= 0.1");
    gate(build + " T4 finite", finite, "every output of every record");
    gate(build + " T4 R in [0, 4]", in_range, "VmfLobe per channel and VmfLobeFast, sweep / seams / grid");
    gate(build + " T4 R exactly 1 at n.v <= 0, 0 at n.l <= 0", back_facing_one && below_zero,
         format("n.v <= 0: %s, n.l <= 0: %s", back_facing_one ? "1" : "NOT 1", below_zero ? "0" : "NOT 0"));
    gate(build + " T4 fade: R = 1 + (n.v / 0.1) (unfaded twin - 1) below n.v 0.1 (the design's 0.1)",
         fade_worst <= SAME_CODE, format("max %.2e (<= 1e-4 x max(R, 1))", fade_worst));
    for (size_t seam = 0; seam < 2; ++seam) {
      gate(build + ((seam == 0) ? " T4 R across the Eq. 4 / 5 seam" : " T4 R across the Eq. 5 / 6 seam")
               + " (r (1 -+ 1e-4)), u >= 0.2: jump <= 0.03",
           seam_worst[seam] <= 0.03,
           format("max %.4f at %s (u >= 0.1: %.4f)", seam_worst[seam], seam_where[seam].c_str(), seam_wide[seam]));
    }
    gate(build + " T2 reciprocity on the grid (u >= 0.1)", reciprocity_worst <= 1e-5,
         format("max %.2e (<= 1e-5)", reciprocity_worst));
    gate(build + " erf vs std::erf; 0, +-inf exact", erf_worst <= 2e-6 && erf_ends,
         format("max |error| %.2e (<= 2e-6), ends %s", erf_worst, erf_ends ? "exact" : "WRONG"));
  };
  check_build("twin", twin);
  {
    bool back_facing_one = true;
    for (size_t index = 0; index < inputs.size(); ++index) {
      if (kinds[index] == RecordKind::SWEEP && inputs[index].n_dot_l > 0.f && inputs[index].n_dot_v <= 0.f) {
        back_facing_one = back_facing_one && unfaded[index].lobe == std::array<float, 3>{1.f, 1.f, 1.f};
      }
    }
    // The rim the fade removes: sun 45, n.v 0 (u_v held at 1e-4), light behind the camera.
    const float u_l = static_cast<float>(std::cos(45.0 * DEGREE));
    const std::array<float, 3> half = {0.5f, 0.5f, 0.5f};
    control("twin T4 without the fade (R 1 at n.v <= 0)", !back_facing_one,
            format("R at sun 45 / n.v 0 behind, r 0.40, base 0.5: %.2f unfaded, %.2f faded",
                   TwinLobe(u_l, 0.f, LDotV(u_l, 0.f, 0.0), half, 0.4f, {.fade = false})[0],
                   TwinLobe(u_l, 0.f, LDotV(u_l, 0.f, 0.0), half, 0.4f, {})[0]));
  }
  // WARP against the twin (the same code: 1e-4 x max(|twin|, 1) per output), and against the controls' twins.
  const std::vector<std::vector<ProbeOutput>> control_twins = {
      twin_outputs({.series = 1.f}), twin_outputs({.match = AlbedoMatch::REMAP}),
      twin_outputs({.match = AlbedoMatch::NONE}), unfaded,
      twin_outputs({.beta_offset = 0.001f})};
  const std::array<const char*, 5> control_names = {"the preprint's series (no x2)", "the Section 5 remap", "beta 0",
                                                    "no fade", "every beta + 0.001"};
  // The worst |a - b| / max(|b|, 1) over every output of every record (non-finite: infinity), and where.
  const auto difference = [&](const std::vector<ProbeOutput>& outputs, const std::vector<ProbeOutput>& reference,
                              std::string* where) {
    double worst = 0;
    for (size_t index = 0; index < inputs.size(); ++index) {
      const ProbeOutput& a = outputs[index];
      const ProbeOutput& b = reference[index];
      const std::array<std::array<float, 2>, 9> pairs = {{{a.lobe[0], b.lobe[0]},
                                                         {a.lobe[1], b.lobe[1]},
                                                         {a.lobe[2], b.lobe[2]},
                                                         {a.fast, b.fast},
                                                         {a.roughness, b.roughness},
                                                         {a.lobe_at_roughness[0], b.lobe_at_roughness[0]},
                                                         {a.lobe_at_roughness[1], b.lobe_at_roughness[1]},
                                                         {a.lobe_at_roughness[2], b.lobe_at_roughness[2]},
                                                         {a.erf, b.erf}}};
      for (size_t field = 0; field < pairs.size(); ++field) {
        const auto [measured, expected] = pairs[field];
        const double gap = std::abs(static_cast<double>(measured) - expected);
        const double error = std::isfinite(gap) ? gap / std::max(std::abs(static_cast<double>(expected)), 1.0)
                                                : std::numeric_limits<double>::infinity();
        if (error > worst) {
          worst = error;
          const ProbeInput& input = inputs[index];
          *where = format("record %zu (kind %d) output %zu: %.7g vs %.7g at n.l %g, n.v %g, l.v %g, r %g, base %g %g "
                          "%g, rho %g, slider %g",
                          index, static_cast<int>(kinds[index]), field, measured, expected, input.n_dot_l,
                          input.n_dot_v, input.l_dot_v, input.r, input.base[0], input.base[1], input.base[2],
                          input.rho, input.slider);
        }
      }
    }
    return worst;
  };
  const auto compare_with_twin = [&](const std::string& build, const std::vector<ProbeOutput>& outputs) {
    check_build(build, outputs);
    std::string where;
    const double worst = difference(outputs, twin, &where);
    gate(build + " T4 against the twin, <= 1e-4 x max(|twin|, 1)", worst <= SAME_CODE,
         format("max %.2e, %s", worst, where.c_str()));
    for (size_t index = 0; index < control_twins.size(); ++index) {
      const double control_worst = difference(outputs, control_twins[index], &where);
      control(build + " against the twin with " + control_names[index], control_worst > SAME_CODE,
              format("max %.2e", control_worst));
    }
  };

  const std::filesystem::path probe_path =
      std::filesystem::path(repository) / "test" / "tw_vmf_diffuse" / "vmf_probe.hlsl";

  // fxc (cs_5_0, /O3 as the add-ons' CMake) on D3D11 WARP.
  {
    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> messages;
    IncludeBesideParent includes(probe_path.parent_path());
    const HRESULT compiled =
        D3DCompileFromFile(probe_path.c_str(), nullptr, &includes, "main", "cs_5_0",
                           D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.GetAddressOf(), messages.GetAddressOf());
    const std::string message_text =
        (messages != nullptr) ? std::string(static_cast<const char*>(messages->GetBufferPointer())) : "";
    gate("static fxc cs_5_0 /O3 compiles without warnings", SUCCEEDED(compiled) && message_text.empty(),
         message_text.empty() ? "clean" : message_text);
    if (FAILED(compiled)) return setup_failed("fxc compile");
    ComPtr<ID3DBlob> listing;
    if (FAILED(D3DDisassemble(code->GetBufferPointer(), code->GetBufferSize(), 0, nullptr, listing.GetAddressOf()))) {
      return setup_failed("fxc disassembly");
    }
    const std::string assembly(static_cast<const char*>(listing->GetBufferPointer()));
    const size_t slots = assembly.find("// Approximately");
    gate("static fxc: no indexable temp, no immediate constant buffer",
         assembly.find("dcl_indexableTemp") == std::string::npos
             && assembly.find("dcl_immediateConstantBuffer") == std::string::npos,
         (slots != std::string::npos) ? assembly.substr(slots, assembly.find('\n', slots) - slots) : "");

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                 device.GetAddressOf(), nullptr, context.GetAddressOf()))) {
      return setup_failed("no D3D11 WARP device");
    }
    ComPtr<ID3D11ComputeShader> shader;
    const D3D11_BUFFER_DESC input_desc = {.ByteWidth = static_cast<UINT>(inputs.size() * sizeof(ProbeInput)),
                                          .Usage = D3D11_USAGE_DEFAULT,
                                          .BindFlags = D3D11_BIND_SHADER_RESOURCE,
                                          .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
                                          .StructureByteStride = sizeof(ProbeInput)};
    const D3D11_SUBRESOURCE_DATA input_data = {.pSysMem = inputs.data()};
    ComPtr<ID3D11Buffer> input_buffer;
    D3D11_BUFFER_DESC output_desc = {.ByteWidth = static_cast<UINT>(inputs.size() * sizeof(ProbeOutput)),
                                     .Usage = D3D11_USAGE_DEFAULT,
                                     .BindFlags = D3D11_BIND_UNORDERED_ACCESS,
                                     .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
                                     .StructureByteStride = sizeof(ProbeOutput)};
    ComPtr<ID3D11Buffer> output_buffer;
    const UINT elements = static_cast<UINT>(inputs.size());
    const D3D11_SHADER_RESOURCE_VIEW_DESC input_view_desc = {.Format = DXGI_FORMAT_UNKNOWN,
                                                             .ViewDimension = D3D11_SRV_DIMENSION_BUFFER,
                                                             .Buffer = {.FirstElement = 0, .NumElements = elements}};
    const D3D11_UNORDERED_ACCESS_VIEW_DESC output_view_desc = {.Format = DXGI_FORMAT_UNKNOWN,
                                                               .ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
                                                               .Buffer = {.FirstElement = 0, .NumElements = elements}};
    ComPtr<ID3D11ShaderResourceView> input_view;
    ComPtr<ID3D11UnorderedAccessView> output_view;
    if (FAILED(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                           shader.GetAddressOf()))) {
      return setup_failed("D3D11 compute shader");
    }
    if (FAILED(device->CreateBuffer(&input_desc, &input_data, input_buffer.GetAddressOf()))
        || FAILED(device->CreateBuffer(&output_desc, nullptr, output_buffer.GetAddressOf()))) {
      return setup_failed("D3D11 buffers");
    }
    if (FAILED(device->CreateShaderResourceView(input_buffer.Get(), &input_view_desc, input_view.GetAddressOf()))
        || FAILED(device->CreateUnorderedAccessView(output_buffer.Get(), &output_view_desc,
                                                    output_view.GetAddressOf()))) {
      return setup_failed("D3D11 views");
    }
    output_desc.Usage = D3D11_USAGE_STAGING;
    output_desc.BindFlags = 0;
    output_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;
    if (FAILED(device->CreateBuffer(&output_desc, nullptr, staging.GetAddressOf()))) {
      return setup_failed("D3D11 staging buffer");
    }
    context->CSSetShader(shader.Get(), nullptr, 0);
    context->CSSetShaderResources(0, 1, input_view.GetAddressOf());
    context->CSSetUnorderedAccessViews(0, 1, output_view.GetAddressOf(), nullptr);
    context->Dispatch(static_cast<UINT>(inputs.size() / GROUP), 1, 1);
    context->CopyResource(staging.Get(), output_buffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return setup_failed("D3D11 readback map");
    std::vector<ProbeOutput> outputs(inputs.size());
    std::memcpy(outputs.data(), mapped.pData, outputs.size() * sizeof(ProbeOutput));
    context->Unmap(staging.Get(), 0);
    compare_with_twin("fxc/D3D11 WARP", outputs);
  }

  // dxc (-O3 as the helper's build.ps1) at cs_6_5, run on D3D12 WARP, and at cs_6_9 (the helper's OMM builds), checked
  // statically; dxil.dll first, so dxcompiler.dll signs with the copy beside it.
  {
    if (LoadLibraryA((dxc_folder + "\\dxil.dll").c_str()) == nullptr) return setup_failed("no dxil.dll");
    const HMODULE dxc_module = LoadLibraryA((dxc_folder + "\\dxcompiler.dll").c_str());
    if (dxc_module == nullptr) return setup_failed("no dxcompiler.dll");
    const auto dxc_create = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(dxc_module, "DxcCreateInstance"));
    if (dxc_create == nullptr) return setup_failed("no DxcCreateInstance");
    ComPtr<IDxcUtils> utils;
    ComPtr<IDxcCompiler3> compiler;
    ComPtr<IDxcIncludeHandler> includes;
    ComPtr<IDxcBlobEncoding> source;
    if (FAILED(dxc_create(CLSID_DxcUtils, IID_PPV_ARGS(&utils)))
        || FAILED(dxc_create(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)))
        || FAILED(utils->CreateDefaultIncludeHandler(&includes))) {
      return setup_failed("dxc instances");
    }
    if (FAILED(utils->LoadFile(probe_path.c_str(), nullptr, &source))) return setup_failed("dxc probe source");
    const DxcBuffer source_buffer = {
        .Ptr = source->GetBufferPointer(), .Size = source->GetBufferSize(), .Encoding = DXC_CP_ACP};
    // The probe at `target`, its static gates; nullptr when it does not compile.
    const auto compile = [&](const wchar_t* target, const char* name) {
      std::vector<LPCWSTR> arguments = {probe_path.c_str(), L"-T", target, L"-E", L"main", L"-O3"};
      ComPtr<IDxcResult> result;
      HRESULT status = E_FAIL;
      ComPtr<IDxcBlob> object;
      if (FAILED(compiler->Compile(&source_buffer, arguments.data(), static_cast<UINT32>(arguments.size()),
                                   includes.Get(), IID_PPV_ARGS(&result)))
          || FAILED(result->GetStatus(&status))) {
        std::printf("FAIL setup: dxc %s compile call\n", name);
        return object;
      }
      ComPtr<IDxcBlobUtf8> errors;
      result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
      const std::string error_text =
          (errors != nullptr && errors->GetStringLength() > 0) ? errors->GetStringPointer() : "";
      gate(std::string("static dxc ") + name + " -O3 compiles without warnings",
           SUCCEEDED(status) && error_text.empty(),
           error_text.empty() ? "clean" : error_text);
      if (FAILED(status)) return object;
      result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr);
      const DxcBuffer object_buffer = {.Ptr = object->GetBufferPointer(), .Size = object->GetBufferSize()};
      ComPtr<IDxcResult> disassembled;
      ComPtr<IDxcBlobUtf8> listing;
      if (FAILED(compiler->Disassemble(&object_buffer, IID_PPV_ARGS(&disassembled)))
          || FAILED(disassembled->GetOutput(DXC_OUT_DISASSEMBLY, IID_PPV_ARGS(&listing), nullptr))) {
        std::printf("FAIL setup: dxc %s disassembly\n", name);
        return ComPtr<IDxcBlob>();
      }
      const std::string assembly = listing->GetStringPointer();
      gate(std::string("static dxc ") + name + ": no alloca, no wave op, no half",
           assembly.find("alloca") == std::string::npos && assembly.find("dx.op.wave") == std::string::npos
               && assembly.find(" half ") == std::string::npos,
           format("DXIL %zu bytes", static_cast<size_t>(object->GetBufferSize())));
      return object;
    };
    if (compile(L"cs_6_9", "cs_6_9") == nullptr) return setup_failed("dxc cs_6_9");
    const ComPtr<IDxcBlob> object = compile(L"cs_6_5", "cs_6_5");
    if (object == nullptr) return setup_failed("dxc cs_6_5");

    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter> warp;
    ComPtr<ID3D12Device> device;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)))
        || FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) {
      return setup_failed("no D3D12 WARP device");
    }
    const std::array<D3D12_ROOT_PARAMETER, 2> parameters = {{
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV, .Descriptor = {.ShaderRegister = 0, .RegisterSpace = 0}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV, .Descriptor = {.ShaderRegister = 0, .RegisterSpace = 0}},
    }};
    const D3D12_ROOT_SIGNATURE_DESC root_desc = {.NumParameters = static_cast<UINT>(parameters.size()),
                                                 .pParameters = parameters.data()};
    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> root_errors;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    if (FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &root_errors))
        || FAILED(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                              IID_PPV_ARGS(&root)))) {
      return setup_failed("D3D12 root signature");
    }
    const D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {
        .pRootSignature = root.Get(),
        .CS = {.pShaderBytecode = object->GetBufferPointer(), .BytecodeLength = object->GetBufferSize()}};
    if (FAILED(device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&pipeline)))) {
      return setup_failed("D3D12 pipeline");
    }
    const auto buffer = [&](size_t size, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state,
                            D3D12_RESOURCE_FLAGS flags) {
      const D3D12_HEAP_PROPERTIES heap_properties = {.Type = heap};
      const D3D12_RESOURCE_DESC desc = {.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
                                        .Width = size,
                                        .Height = 1,
                                        .DepthOrArraySize = 1,
                                        .MipLevels = 1,
                                        .SampleDesc = {.Count = 1},
                                        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                        .Flags = flags};
      ComPtr<ID3D12Resource> resource;
      device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                      IID_PPV_ARGS(&resource));
      return resource;
    };
    const size_t input_size = inputs.size() * sizeof(ProbeInput);
    const size_t output_size = inputs.size() * sizeof(ProbeOutput);
    const ComPtr<ID3D12Resource> upload =
        buffer(input_size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
    const ComPtr<ID3D12Resource> output = buffer(output_size, D3D12_HEAP_TYPE_DEFAULT,
                                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    const ComPtr<ID3D12Resource> readback =
        buffer(output_size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE);
    if (upload == nullptr || output == nullptr || readback == nullptr) return setup_failed("D3D12 buffers");
    void* mapped = nullptr;
    if (FAILED(upload->Map(0, nullptr, &mapped))) return setup_failed("D3D12 upload map");
    std::memcpy(mapped, inputs.data(), input_size);
    upload->Unmap(0, nullptr);
    ComPtr<ID3D12CommandQueue> queue;
    const D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    if (FAILED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)))
        || FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)))
        || FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), pipeline.Get(),
                                            IID_PPV_ARGS(&list)))
        || FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
      return setup_failed("D3D12 queue, allocator, list or fence");
    }
    list->SetComputeRootSignature(root.Get());
    list->SetComputeRootShaderResourceView(0, upload->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(1, output->GetGPUVirtualAddress());
    list->Dispatch(static_cast<UINT>(inputs.size() / GROUP), 1, 1);
    const D3D12_RESOURCE_BARRIER barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                                            .Transition = {.pResource = output.Get(),
                                                           .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                                           .StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                           .StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE}};
    list->ResourceBarrier(1, &barrier);
    list->CopyResource(readback.Get(), output.Get());
    if (FAILED(list->Close())) return setup_failed("D3D12 list close");
    ID3D12CommandList* lists[] = {list.Get()};
    queue->ExecuteCommandLists(1, lists);
    if (FAILED(queue->Signal(fence.Get(), 1)) || FAILED(fence->SetEventOnCompletion(1, nullptr))) {
      return setup_failed("D3D12 fence wait");
    }
    if (FAILED(device->GetDeviceRemovedReason())) return setup_failed("D3D12 WARP device removed");
    std::vector<ProbeOutput> outputs(inputs.size());
    const D3D12_RANGE read_range = {.Begin = 0, .End = output_size};
    if (FAILED(readback->Map(0, &read_range, &mapped))) return setup_failed("D3D12 readback map");
    std::memcpy(outputs.data(), mapped, output_size);
    const D3D12_RANGE written = {.Begin = 0, .End = 0};
    readback->Unmap(0, &written);
    compare_with_twin("dxc/D3D12 WARP", outputs);
  }

  const bool all_ok = gates_failed == 0 && controls_passed == 0;
  std::printf("\n%s: %d gate(s) failed, %d control(s) passed that must fail\n", all_ok ? "ALL OK" : "NOT OK",
              gates_failed, controls_passed);
  return all_ok ? 0 : 1;
}
