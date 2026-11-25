/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Callisto diffuse gates: src/games/twattila/callisto_diffuse.hlsli against its CPU twin (below: the header's math in
// float, pow as exp2(e log2 x) like HLSL), and the twin against a double slide model (c1, c2 in its three alpha_s
// readings, Proxima: slides 85 / 90 / 128, explicit h vectors). The twin and the probe take their sets and constants
// from the one src/games/twattila/callisto_diffuse.h; the slide model takes the talk's knobs and the design's knob
// table from slide_points.h (make_points.py). The header runs in callisto_probe.hlsl on WARP, compiled by fxc (cs_5_0,
// D3D11) and dxc (cs_6_5, D3D12); dxc's cs_6_9 build is checked statically only.
// Gates (each PASS / FAIL line):
//   T-slides  the slide model at slide_points.h's points (make_points.py's numpy reading) within 1e-9 relative; its
//             integrals (E at view 0 / 45 / 70 / 85, bond, c2's R cos rows) against make_points.py's on the same grid
//             within 1e-6 relative; those against the two earlier checks' printed ones (callisto_check.txt,
//             cdr_callisto.txt) within 1e-3 max(|ref|, 0.1) + half a printed unit
//   design    each set's rho and exponents against the design's knob table (10 (1 - knob)) within 1e-6; each DI
//             twin's retro is its parent's, with rho_f 1
//   bonds     each kept set's inverse_bond x the knob model's unfaded bond albedo (double, T-bond's 96^3 grid) within
//             1e-6; an unkept set's inverse_bond exactly 1
//   T-twin    the twin against the knob model's c1 x inverse_bond with the same u clamp, cap and fade, every set, u
//             from 1e-4: 1e-5 relative; and against the raw c1 x inverse_bond over u >= 0.1 where it is <= 4
//   T2        c1's reciprocity on the twin and both WARP builds, u >= 0.1: 1e-6 relative
//   T-bond    each kept set's bond albedo = 1 within 1e-3 on a 96^3 grid with the fade in (twin), and the cap acts
//             on none of them; a scoring-only set the cap acts on (A + F) gets an INFO line with its bonds instead
//   T3        amount 0, and the talk's defaults (rho 1) at amount 1: exactly 1.0f (twin, fxc WARP, dxc WARP)
//   T4        finite (the twin's std::isfinite: WARP's min / max hide NaNs), R in [0, 4], exactly 1 at amount 0,
//             n.l <= 0 and n.v <= 0, the fade R = 1 + (n.v / 0.1) (unfaded - 1) below n.v 0.1 (the design's 0.1; the
//             unfaded twin carries the amount), over the knob-bound sets and the edge records
//   WARP      WARP against the twin within 1e-4 x max(|twin|, 1), every Callisto output of every record
//   T-vmf     V16 / V40 (twin) against the merged VmfLobe (WARP) at base 0.5 on the closed grid u_l, u_v in
//             {0.2, 0.205, ..., 1} (161, edges in), phi {0, 1, ..., 180} (181): weighted (u_l u_v) rms <= 2 %; max
//             <= 40 % (V16), <= 20 % (V40)
//   static    fxc and dxc (cs_6_5, cs_6_9) without warnings; no indexable temp / immediate constant buffer (fxc), no
//             alloca, wave op or half (dxc); the cost line: fxc slots and DXIL instructions per set
// Controls (each must FAIL its gate; CONTROL lines): T-slides with 5n read as n^5, with r(x) = 2x, with theta_d /
// theta_h swapped inside alpha_r; design and bonds with V16's n_r off by 1e-3; T-twin without the fade; T2 with cos_h
// from n.l alone (twin) and with c2 at o 0.25 (slide model); T-bond with inverse_bond x 1.01; T3 with rho_r 1 + 2^-20; T4 with neither the exponent-0 fold nor the base clamp (NaN at h = n,
// n_r 1, and at l = v, n_f 1), and the fade at 0.08; WARP against a twin with two exponents swapped; T-vmf with
// Lambert.
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
#include <utility>
#include <vector>

#include "../../src/games/twattila/callisto_diffuse.h"
#include "slide_points.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr uint32_t ONE_BITS = 0x3F800000u;
constexpr uint32_t GROUP = 64;
constexpr double SAME_CODE = 1e-4;  // |WARP - twin| <= SAME_CODE max(|twin|, 1)
constexpr double DEGREE = std::numbers::pi / 180.0;
// The design's fade cosine and cap, not the header's: a slip there moves the twin too.
constexpr float DESIGN_FADE_COSINE = 0.1f;
constexpr float DESIGN_LOBE_MAX = 4.f;

struct NamedSet {
  const char* name;
  CallistoSet set;
  bool scoring_only;  // never shipped: the cap may act on it (T-bond then prints its bonds instead of gating them)
};

// The header's sets in callisto_probe.hlsl's ProbeOutput order, then the scoring-only ones; slide_points.h's
// DESIGN_SETS order. A + F: A plus red fabric 2's diffuse Fresnel (slide 110: rho_f 25, n_f 0.6, m_f 0.97); rho_f > 1
// is not a shipping set. The cap acts on it at grazing views: its bond albedo is 0.9890 with the fade in, 0.9907
// unfaded, 1 uncapped.
constexpr size_t PROBE_SETS = 6;
const std::array<NamedSet, 7> NAMED_SETS = {{{"A", CALLISTO_SET_A, false},
                                             {"V16", CALLISTO_SET_V16, false},
                                             {"V16_DI", CALLISTO_SET_V16_DI, false},
                                             {"V40", CALLISTO_SET_V40, false},
                                             {"V40_DI", CALLISTO_SET_V40_DI, false},
                                             {"PLANTED", CALLISTO_SET_PLANTED, false},
                                             {"A+F", {3.f, 5.f, 2.5f, 25.f, 4.f, 0.3f, 1.f / 1.1816264f}, true}}};
static_assert(std::size(DESIGN_SETS) == NAMED_SETS.size());

// The twin's switches: all on / off as shipped; the rest are controls.
struct TwinOptions {
  bool fold = true;        // exponent 0 -> factor 1
  bool clamp_base = true;  // pow's base >= CALLISTO_MIN_BASE
  bool fade = true;
  float fade_cosine = CALLISTO_FADE_COSINE;
  float bond_scale = 1.f;
  float lobe_max = CALLISTO_LOBE_MAX;
  bool swap_exponents = false;      // retro_exponent <-> retro_tangent_exponent
  bool cos_h_from_n_dot_l = false;  // cos_h = 2 n.l / sqrt(2 + 2 l.v)
};

// The header's twin: CallistoLobe in float, HLSL's pow as exp2(e log2 x), saturate as std::clamp (which keeps NaN, as
// std::min does with a NaN first operand; GPU min / max would drop it).
float TwinLobe(const CallistoSet& set, float n_dot_l, float n_dot_v, float l_dot_v, float amount,
               const TwinOptions& options) {
  if (amount <= 0.f) return 1.f;
  if (n_dot_l <= 0.f) return 1.f;
  if (n_dot_v <= 0.f) return 1.f;
  const float u_l = std::clamp(n_dot_l, CALLISTO_MIN_COSINE, 1.f);
  const float u_v = std::clamp(n_dot_v, CALLISTO_MIN_COSINE, 1.f);
  const float min_base = options.clamp_base ? CALLISTO_MIN_BASE : 0.f;
  const auto power = [&](float base, float exponent) {
    if (options.fold && exponent == 0.f) return 1.f;
    return std::exp2(exponent * std::log2(std::max(base, min_base)));
  };
  const float cos_d = std::sqrt(std::clamp(0.5f + (0.5f * l_dot_v), 0.f, 1.f));
  const float cosine_sum = options.cos_h_from_n_dot_l ? (2.f * u_l) : (u_l + u_v);
  const float cos_h =
      std::clamp(cosine_sum * (1.f / std::sqrt(std::max(2.f + (2.f * l_dot_v), CALLISTO_MIN_BASE))), 0.f, 1.f);
  const float retro_exponent = options.swap_exponents ? set.retro_tangent_exponent : set.retro_exponent;
  const float retro_tangent_exponent = options.swap_exponents ? set.retro_exponent : set.retro_tangent_exponent;
  const float retro_mask = power(1.f - cos_h, retro_exponent) * power(cos_d, retro_tangent_exponent);
  const float fresnel_mask = power(1.f - cos_d, set.fresnel_exponent) * power(cos_h, set.fresnel_tangent_exponent);
  const float c1 = (1.f + ((set.fresnel - 1.f) * fresnel_mask)) * (1.f + ((set.retro - 1.f) * retro_mask));
  const float lobe = std::min(c1 * set.inverse_bond * options.bond_scale, options.lobe_max);
  const float fade = options.fade ? std::clamp(n_dot_v / options.fade_cosine, 0.f, 1.f) : 1.f;
  return 1.f + ((amount * fade) * (lobe - 1.f));
}

using Vector = std::array<double, 3>;

double Dot(const Vector& a, const Vector& b) { return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]); }

// A unit direction theta degrees from n = +z, at azimuth phi degrees (phi 0 = the light's side: backscatter).
Vector Direction(double theta_degrees, double phi_degrees) {
  const double theta = theta_degrees * DEGREE;
  const double phi = phi_degrees * DEGREE;
  return {std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta)};
}

// How the slide model reads slide 85's exponents and alpha_r; the default is the design's reading, the rest are
// T-slides' controls.
enum class FalloffReading : uint8_t { FIVE_R, R_TO_FIVE, TWO_X };

struct SlideOptions {
  FalloffReading falloff = FalloffReading::FIVE_R;  // 5 r(x), r(x)^5 ("5n" read as n^5), 5 (2x) (r(x) = 2x)
  bool swap_retro_angles = false;                   // alpha_r = h(theta_d, n_r, theta_h, m_r)
};

// The talk's R = c1 x c2 x Proxima (slides 85 / 90 / 128), in double, from l and v (unit, n = +z) with h =
// normalize(l + v), as make_points.py. c2 = 1 and Proxima = 1 exactly at o 0 and alpha 0.
double SlideLobe(const SlideModel& model, const Vector& l, const Vector& v, const SlideOptions& options) {
  const Vector sum = {l[0] + v[0], l[1] + v[1], l[2] + v[2]};
  const double length = std::sqrt(Dot(sum, sum));
  const Vector h = {sum[0] / length, sum[1] / length, sum[2] / length};
  const double cos_i = l[2];
  // Clipped to [0, 1] before any power (the slide's bars; l = v gives 1 - cos_h = -2e-16 on closed grids).
  const double cos_d = std::clamp(Dot(l, h), 0.0, 1.0);
  const double cos_h = std::clamp(h[2], 0.0, 1.0);
  const auto bar = [](double x) { return std::max(x, 0.0); };
  const auto exponent = [&](double falloff) {
    switch (options.falloff) {
      case FalloffReading::FIVE_R:    return 5.0 * 2.0 * (1.0 - falloff);
      case FalloffReading::R_TO_FIVE: return std::pow(2.0 * (1.0 - falloff), 5.0);
      case FalloffReading::TWO_X:     return 5.0 * 2.0 * falloff;
    }
    return 0.0;
  };
  // slide 85: h(theta, n, phi, m) = (1 - bar(cos theta))^(5 n) bar(cos phi)^(5 m), n = r(falloff); the bars are the
  // clip above.
  const auto mask = [&](double cos_theta, double falloff, double cos_phi, double tangent_falloff) {
    return std::pow(1.0 - cos_theta, exponent(falloff)) * std::pow(cos_phi, exponent(tangent_falloff));
  };
  const double alpha_f = mask(cos_d, model.fresnel_falloff, cos_h, model.fresnel_tangent_falloff);
  const double alpha_r = options.swap_retro_angles
                             ? mask(cos_d, model.retro_falloff, cos_h, model.retro_tangent_falloff)
                             : mask(cos_h, model.retro_falloff, cos_d, model.retro_tangent_falloff);
  const double c1 = (1.0 + ((model.fresnel - 1.0) * alpha_f)) * (1.0 + ((model.retro - 1.0) * alpha_r));
  double theta_d = cos_d;
  double theta_h = cos_h;
  if (model.alpha_s_reading != 2) {
    const double scale = (model.alpha_s_reading == 1) ? (0.5 * std::numbers::pi) : 1.0;
    theta_d = std::acos(cos_d) / scale;
    theta_h = std::acos(cos_h) / scale;
  }
  const double alpha_s = (1.0 - std::pow(1.0 - theta_d, 3.0)) * (1.0 - std::pow(1.0 - theta_h, 3.0));
  const double edge = alpha_s * model.terminator_length;
  double step = (cos_i > 0.0) ? 1.0 : 0.0;
  if (edge != 0.0) {
    step = std::clamp(cos_i / edge, 0.0, 1.0);
  }
  const double c2 = 1.0 + ((((step * step * (3.0 - (2.0 * step))) - 1.0) * alpha_s) * model.terminator);
  const double proxima =
      1.0 + (model.proxima_alpha * (-0.55 + (0.19 / bar(cos_i))) * (1.0 - std::sqrt(bar(-Dot(v, l)))));
  return c1 * c2 * proxima;
}

// Gauss-Legendre on (0, 1): {node, weight}, weights summing to 1.
std::vector<std::array<double, 2>> GaussLegendre(size_t count) {
  std::vector<std::array<double, 2>> rule(count);
  const double size = static_cast<double>(count);
  for (size_t i = 0; i < count; ++i) {
    double z = std::cos(std::numbers::pi * (static_cast<double>(i) + 0.75) / (size + 0.5));
    double derivative = 1;
    for (int iteration = 0; iteration < 100; ++iteration) {
      double p1 = 1;
      double p2 = 0;
      for (size_t order = 1; order <= count; ++order) {
        const double p3 = p2;
        const double n = static_cast<double>(order);
        p2 = p1;
        p1 = ((((2.0 * n) - 1.0) * z * p2) - ((n - 1.0) * p3)) / n;
      }
      derivative = size * ((z * p1) - p2) / ((z * z) - 1.0);
      const double previous = z;
      z = previous - (p1 / derivative);
      if (std::abs(z - previous) < 1e-15) break;
    }
    rule[i] = {0.5 * (1.0 - z), 1.0 / ((1.0 - (z * z)) * derivative * derivative)};
  }
  return rule;
}

// l.v of two directions with cosines u_l, u_v to the normal and azimuths phi degrees apart.
float LDotV(float u_l, float u_v, double phi_degrees) {
  const float sines = std::sqrt((1.f - (u_l * u_l)) * (1.f - (u_v * u_v)));
  return (u_l * u_v) + (sines * static_cast<float>(std::cos(phi_degrees * DEGREE)));
}

// callisto_probe.hlsl's records.
struct ProbeInput {
  CallistoSet set = {};
  float n_dot_l = 0;
  float n_dot_v = 0;
  float l_dot_v = 0;
  float amount = 0;
};
static_assert(sizeof(ProbeInput) == 44);

struct ProbeOutput {
  std::array<float, PROBE_SETS> named = {};  // NAMED_SETS' order
  float own = 0;                             // the record's set
};
static_assert(sizeof(ProbeOutput) == 28);

enum class RecordKind : uint8_t { T3_ZERO, T3_DEFAULTS, T3_CONTROL, SWEEP, FADE, GRID, PAD };

// fxc.exe's include rule (the add-ons' build): a nested include resolves against the including file's folder
// (D3D_COMPILE_STANDARD_FILE_INCLUDE takes the first file's folder at every level). As test/tw_vmf_diffuse.
class IncludeBesideParent : public ID3DInclude {
 public:
  explicit IncludeBesideParent(const std::filesystem::path& folder) : folder_(folder) {}

  HRESULT __stdcall Open(D3D_INCLUDE_TYPE /*type*/, LPCSTR name, LPCVOID parent, LPCVOID* data,
                         UINT* size) noexcept override {
    std::filesystem::path folder = folder_;
    for (const File& file : files_) {
      if (file.text.data() == parent) {
        folder = file.folder;
      }
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
    if (!passed) {
      ++gates_failed;
    }
  };
  // A control must fail its gate.
  const auto control = [&](const std::string& name, bool gate_failed, const std::string& detail) {
    std::printf("CONTROL %s %s: %s\n", gate_failed ? "fails as required" : "PASSES (wrong)", name.c_str(),
                detail.c_str());
    if (!gate_failed) {
      ++controls_passed;
    }
  };
  const auto setup_failed = [](const char* step) {
    std::printf("FAIL setup: %s\n", step);
    return 1;
  };
  const auto format = [](const char* pattern, auto... values) {
    std::array<char, 2048> text = {};
    std::snprintf(text.data(), text.size(), pattern, values...);
    return std::string(text.data());
  };
  // |a / b - 1|, infinite when either is not finite (a NaN never passes a bar).
  const auto relative = [](double a, double b) {
    const double gap = std::abs((a / b) - 1.0);
    return std::isfinite(gap) ? gap : std::numeric_limits<double>::infinity();
  };

  // T-slides: the slide model at make_points.py's points and against the earlier checks' integrals; each control
  // reading must fail one of them.
  {
    const std::vector<std::array<double, 2>> light_rule = GaussLegendre(128);
    const std::vector<std::array<double, 2>> bond_light_rule = GaussLegendre(96);
    const std::vector<std::array<double, 2>> view_rule = GaussLegendre(64);
    // E(view) = (1 / pi) Int R cos dw over the light hemisphere: Gauss-Legendre in cos, midpoints in phi over [0, pi]
    // (R is even in the light's azimuth about the view's plane).
    const auto albedo = [&](const SlideModel& model, const Vector& v, const SlideOptions& options,
                            const std::vector<std::array<double, 2>>& rule, int azimuths) {
      double sum = 0;
      for (const auto& [mu, weight] : rule) {
        double phi_sum = 0;
        for (int step = 0; step < azimuths; ++step) {
          const double phi = (static_cast<double>(step) + 0.5) * 180.0 / azimuths;
          phi_sum += SlideLobe(model, Direction(std::acos(mu) / DEGREE, phi), v, options);
        }
        sum += weight * mu * phi_sum / azimuths;
      }
      return 2.0 * sum;
    };
    // [points worst, integrals worst (in units of each tolerance)] and where.
    const auto check = [&](const SlideOptions& options, std::string* where) {
      double point_worst = 0;
      for (const SlidePoint& point : SLIDE_POINTS) {
        const double lobe = SlideLobe(SLIDE_MODELS[point.model], Direction(point.theta_l, 0.0),
                                      Direction(point.theta_v, point.phi), options);
        point_worst = std::max(point_worst, relative(lobe, point.lobe));
      }
      double integral_worst = 0;
      for (const SlideIntegral& integral : SLIDE_INTEGRALS) {
        const SlideModel& model = SLIDE_MODELS[integral.model];
        double value = 0;
        switch (integral.kind) {
          case 0:
            value = albedo(model, Direction(integral.at, 0.0), options, light_rule, 128);
            break;
          case 1:
            for (const auto& [mu, weight] : view_rule) {
              value += 2.0 * weight * mu
                       * albedo(model, Direction(std::acos(mu) / DEGREE, 0.0), options, bond_light_rule, 96);
            }
            break;
          default:
            value = integral.at
                    * SlideLobe(model, {std::sqrt(1.0 - (integral.at * integral.at)), 0.0, integral.at},
                                {0.0, 0.0, 1.0}, options);
            break;
        }
        const double gap = relative(value, integral.value);
        if (gap > integral_worst) {
          integral_worst = gap;
          *where = format("%s kind %d at %g: %.6f vs %.6f", model.name, integral.kind, integral.at, value,
                          integral.value);
        }
      }
      return std::array<double, 2>{point_worst, integral_worst};
    };
    std::string where;
    const std::array<double, 2> reading = check({}, &where);
    gate(format("T-slides the slide model at make_points.py's %zu points (10 talk fits, Proxima, c2 x 3 readings)",
                std::size(SLIDE_POINTS)),
         reading[0] <= 1e-9, format("max relative %.2e (<= 1e-9)", reading[0]));
    gate(format("T-slides its integrals against make_points.py's full-precision ones (%zu: E, bonds, c2's R cos)",
                std::size(SLIDE_INTEGRALS)),
         reading[1] <= 1e-6, format("max relative %.2e (<= 1e-6, the same grid) at %s", reading[1], where.c_str()));
    // The earlier checks' printed integrals against make_points.py's (other quadratures, printed to 3-4 decimals):
    // within 1e-3 max(|ref|, 0.1) + half a unit of the printed last digit.
    double printed_worst = 0;
    std::string printed_where;
    for (const SlidePrinted& printed : SLIDE_PRINTED) {
      for (const SlideIntegral& integral : SLIDE_INTEGRALS) {
        if (integral.model != printed.model || integral.kind != printed.kind || integral.at != printed.at) continue;
        const double tolerance =
            (1e-3 * std::max(std::abs(printed.value), 0.1)) + (0.5 * std::pow(10.0, -printed.decimals));
        const double units = std::abs(integral.value - printed.value) / tolerance;
        if (units > printed_worst) {
          printed_worst = units;
          printed_where = format("%s %s kind %d at %g: %.6f vs %.*f", printed.source,
                                 SLIDE_MODELS[printed.model].name, printed.kind, printed.at, integral.value,
                                 printed.decimals, printed.value);
        }
      }
    }
    gate(format("T-slides the earlier checks' %zu printed integrals vs make_points.py's", std::size(SLIDE_PRINTED)),
         printed_worst <= 1.0,
         format("worst %.2f x the tolerance (1e-3 max(|ref|, 0.1) + half a printed unit), at %s", printed_worst,
                printed_where.c_str()));
    const std::array<std::pair<const char*, SlideOptions>, 3> misreadings = {{
        {"5n read as n^5", {.falloff = FalloffReading::R_TO_FIVE}},
        {"r(x) = 2x", {.falloff = FalloffReading::TWO_X}},
        {"theta_d / theta_h swapped inside alpha_r", {.swap_retro_angles = true}},
    }};
    for (const auto& [name, options] : misreadings) {
      const std::array<double, 2> misread = check(options, &where);
      control(std::string("T-slides with ") + name, misread[0] > 1e-9 || misread[1] > 1e-6,
              format("points max relative %.2e; integrals max relative %.2e at %s", misread[0], misread[1],
                     where.c_str()));
    }
  }

  // design and bonds: each set against slide_points.h's knob table (the design's, independent of the header): rho and
  // the exponents 10 (1 - knob), each DI twin's retro against its parent's, and inverse_bond against the knob model's
  // unfaded bond albedo in double on T-bond's grid (Gauss-Legendre 96 x 96, 96 midpoints in phi over [0, pi]).
  {
    const std::vector<std::array<double, 2>> rule = GaussLegendre(96);
    const auto bond_of = [&](const SlideModel& model) {
      double sum = 0;
      for (const auto& [u_l, weight_l] : rule) {
        for (const auto& [u_v, weight_v] : rule) {
          const Vector l = {std::sqrt(1.0 - (u_l * u_l)), 0.0, u_l};
          double phi_sum = 0;
          for (int step = 0; step < 96; ++step) {
            const double phi = (static_cast<double>(step) + 0.5) * std::numbers::pi / 96.0;
            const double sine_v = std::sqrt(1.0 - (u_v * u_v));
            phi_sum += SlideLobe(model, l, {sine_v * std::cos(phi), sine_v * std::sin(phi), u_v}, {});
          }
          sum += weight_l * weight_v * u_l * u_v * phi_sum / 96.0;
        }
      }
      return 4.0 * sum;
    };
    // max |header - design| over rho and the exponents.
    const auto knob_gap = [](const CallistoSet& set, const SlideModel& model) {
      const std::array<double, 6> header = {set.retro,   set.retro_exponent,   set.retro_tangent_exponent,
                                            set.fresnel, set.fresnel_exponent, set.fresnel_tangent_exponent};
      const std::array<double, 6> design = {
          model.retro,   10.0 * (1.0 - model.retro_falloff),   10.0 * (1.0 - model.retro_tangent_falloff),
          model.fresnel, 10.0 * (1.0 - model.fresnel_falloff), 10.0 * (1.0 - model.fresnel_tangent_falloff)};
      double worst = 0;
      for (size_t knob = 0; knob < header.size(); ++knob) {
        worst = std::max(worst, std::abs(header[knob] - design[knob]));
      }
      return worst;
    };
    // |inverse_bond x bond - 1| for a kept set, |inverse_bond - 1| for an unkept one; the bond through `bond`.
    const auto bond_gap = [&](const CallistoSet& set, const DesignSet& design, double* bond) {
      *bond = bond_of(design.model);
      return design.kept ? std::abs((set.inverse_bond * *bond) - 1.0) : std::abs(set.inverse_bond - 1.0);
    };
    bool names_match = true;
    bool twins_match = true;
    double knob_worst = 0;
    double bond_worst = 0;
    std::string rows;
    for (size_t index = 0; index < NAMED_SETS.size(); ++index) {
      const NamedSet& named = NAMED_SETS[index];
      const DesignSet& design = DESIGN_SETS[index];
      names_match = names_match && std::string(named.name) == design.model.name;
      knob_worst = std::max(knob_worst, knob_gap(named.set, design.model));
      if (design.parent >= 0) {
        const CallistoSet& parent = NAMED_SETS[design.parent].set;
        twins_match = twins_match && named.set.retro == parent.retro
                      && named.set.retro_exponent == parent.retro_exponent
                      && named.set.retro_tangent_exponent == parent.retro_tangent_exponent && named.set.fresnel == 1.f;
      }
      double bond = 0;
      bond_worst = std::max(bond_worst, bond_gap(named.set, design, &bond));
      rows += format(" %s %.7f%s;", named.name, bond, design.kept ? "" : " (unkept)");
    }
    gate("design every set's rho and exponents vs slide_points.h's knob table, 10 (1 - knob)",
         names_match && knob_worst <= 1e-6,
         format("names %s; max |header - design| %.2e (<= 1e-6)", names_match ? "match" : "DIFFER", knob_worst));
    gate("design each DI twin's retro is its parent's, rho_f 1", twins_match, twins_match ? "V16_DI, V40_DI" : "no");
    gate("bonds inverse_bond x the knob model's unfaded bond albedo in double = 1, every kept set; unkept 1",
         bond_worst <= 1e-6, format("max gap %.2e (<= 1e-6):%s", bond_worst, rows.c_str()));
    DesignSet off = DESIGN_SETS[1];
    off.model.retro_falloff += 1e-3;
    double off_bond = 0;
    const double off_knob = knob_gap(NAMED_SETS[1].set, off.model);
    const double off_bond_gap = bond_gap(NAMED_SETS[1].set, off, &off_bond);
    control(format("design and bonds with %s's n_r off by 1e-3", NAMED_SETS[1].name),
            off_knob > 1e-6 && off_bond_gap > 1e-6,
            format("knob gap %.2e, bond gap %.2e (bond %.7f)", off_knob, off_bond_gap, off_bond));
  }

  // T-twin: the twin against the double slide model of the design's knobs, c1 x inverse_bond, evaluated at the twin's
  // own (float) n.l, n.v and l.v: (1) with the header's u clamp, cap and fade, u from 1e-4; (2) raw over u >= 0.1,
  // where none of the three acts (records where the raw value passes the cap are counted, not compared). Its control:
  // the twin without the fade.
  {
    // The double model at the twin's inputs: l in the xz plane, v from n.v and l.v.
    const auto slide_at = [](const SlideModel& model, double u_l, double u_v, double l_dot_v) {
      const double sine_l = std::sqrt(1.0 - (u_l * u_l));
      const double sine_v = std::sqrt(1.0 - (u_v * u_v));
      const double v_x = (sine_l > 0.0) ? std::clamp((l_dot_v - (u_l * u_v)) / sine_l, -sine_v, sine_v) : sine_v;
      return SlideLobe(model, {sine_l, 0.0, u_l}, {v_x, std::sqrt(std::max((sine_v * sine_v) - (v_x * v_x), 0.0)), u_v},
                       {});
    };
    const std::array<float, 14> cosines = {1e-4f, 0.01f, 0.03f, 0.05f, 0.08f, 0.1f, 0.2f,
                                           0.3f,  0.45f, 0.6f,  0.75f, 0.9f,  0.97f, 1.f};
    double clamped_worst = 0;
    double raw_worst = 0;
    size_t capped = 0;
    std::string clamped_where;
    std::string raw_where;
    // The clamped pass's worst gap for a twin with `options` (the gate's twin when `gate_twin`: both passes recorded).
    const auto compare = [&](const TwinOptions& options, bool gate_twin) {
      double worst = 0;
      for (size_t index = 0; index < NAMED_SETS.size(); ++index) {
        const NamedSet& named = NAMED_SETS[index];
        const SlideModel& model = DESIGN_SETS[index].model;
        for (const float n_dot_l : cosines) {
          for (const float n_dot_v : cosines) {
            for (int step = 0; step <= 12; ++step) {
              const float l_dot_v = LDotV(n_dot_l, n_dot_v, 15.0 * step);
              const float twin = TwinLobe(named.set, n_dot_l, n_dot_v, l_dot_v, 1.f, options);
              const double u_l =
                  std::clamp(static_cast<double>(n_dot_l), static_cast<double>(CALLISTO_MIN_COSINE), 1.0);
              const double u_v =
                  std::clamp(static_cast<double>(n_dot_v), static_cast<double>(CALLISTO_MIN_COSINE), 1.0);
              const double raw = slide_at(model, u_l, u_v, l_dot_v) * named.set.inverse_bond;
              const double fade = std::clamp(static_cast<double>(n_dot_v) / DESIGN_FADE_COSINE, 0.0, 1.0);
              const double clamped = 1.0 + (fade * (std::min(raw, static_cast<double>(DESIGN_LOBE_MAX)) - 1.0));
              const double gap = relative(twin, clamped);
              worst = std::max(worst, gap);
              if (!gate_twin) continue;
              if (gap > clamped_worst) {
                clamped_worst = gap;
                clamped_where = format("%s n.l %g n.v %g phi %d: %.7f vs %.7f", named.name, n_dot_l, n_dot_v,
                                       15 * step, twin, clamped);
              }
              if (n_dot_l < 0.1f || n_dot_v < 0.1f) continue;
              if (raw > DESIGN_LOBE_MAX) {
                ++capped;
                continue;
              }
              const double raw_gap = relative(twin, raw);
              if (raw_gap > raw_worst) {
                raw_worst = raw_gap;
                raw_where = format("%s n.l %g n.v %g phi %d", named.name, n_dot_l, n_dot_v, 15 * step);
              }
            }
          }
        }
      }
      return worst;
    };
    compare({}, true);
    gate("T-twin the twin vs the knob model's c1 x inverse_bond with the u clamp, cap and fade, every set, u >= 1e-4",
         clamped_worst <= 1e-5, format("max relative %.2e (<= 1e-5) at %s", clamped_worst, clamped_where.c_str()));
    gate("T-twin the twin vs the knob model's raw c1 x inverse_bond, every set, u >= 0.1", raw_worst <= 1e-5,
         format("max relative %.2e (<= 1e-5) at %s; %zu records above the cap skipped", raw_worst, raw_where.c_str(),
                capped));
    const double unfaded_worst = compare({.fade = false}, false);
    control("T-twin with the twin's fade off", unfaded_worst > 1e-5, format("max relative %.2e", unfaded_worst));
  }

  // T-bond: each kept set's bond albedo with the fade in (twin, amount 1; Gauss-Legendre 96 x 96, 96 midpoints in phi
  // over [0, pi]), and the control inverse_bond x 1.01. The cap must act on no kept set (it would break brightness
  // kept); a scoring-only set it acts on gets an INFO line instead.
  {
    const std::vector<std::array<double, 2>> rule = GaussLegendre(96);
    const auto kept_bond = [&](const CallistoSet& set, const TwinOptions& options) {
      double sum = 0;
      for (const auto& [u_l, weight_l] : rule) {
        for (const auto& [u_v, weight_v] : rule) {
          double phi_sum = 0;
          for (int step = 0; step < 96; ++step) {
            const double phi = (static_cast<double>(step) + 0.5) * 180.0 / 96.0;
            const float l_dot_v = LDotV(static_cast<float>(u_l), static_cast<float>(u_v), phi);
            phi_sum += TwinLobe(set, static_cast<float>(u_l), static_cast<float>(u_v), l_dot_v, 1.f, options);
          }
          sum += weight_l * weight_v * u_l * u_v * phi_sum / 96.0;
        }
      }
      return 4.0 * sum;
    };
    double worst = 0;
    double control_best = std::numeric_limits<double>::infinity();
    std::string rows;
    std::string cap_acts;
    for (size_t index = 0; index < NAMED_SETS.size(); ++index) {
      const NamedSet& named = NAMED_SETS[index];
      if (!DESIGN_SETS[index].kept) continue;
      const double bond = kept_bond(named.set, {});
      const double unfaded = kept_bond(named.set, {.fade = false});
      const double uncapped = kept_bond(named.set, {.fade = false, .lobe_max = std::numeric_limits<float>::infinity()});
      if (std::abs(uncapped - unfaded) > 1e-6) {
        if (named.scoring_only) {
          std::printf("INFO T-bond %s (scoring only): the cap acts: kept bond %.6f (unfaded %.6f, uncapped %.6f)\n",
                      named.name, bond, unfaded, uncapped);
          continue;
        }
        cap_acts += format(" %s (unfaded %.6f, uncapped %.6f)", named.name, unfaded, uncapped);
      }
      worst = std::max(worst, std::abs(bond - 1.0));
      control_best = std::min(control_best, std::abs(kept_bond(named.set, {.bond_scale = 1.01f}) - 1.0));
      rows += format(" %s %.6f (unfaded %.6f);", named.name, bond, unfaded);
    }
    gate("T-bond each kept set's bond albedo = 1 within 1e-3 (96^3, fade in), the cap acting on none",
         worst <= 1e-3 && cap_acts.empty(),
         format("max |bond - 1| %.2e:%s%s%s", worst, rows.c_str(), cap_acts.empty() ? "" : " the cap acts on:",
                cap_acts.c_str()));
    control("T-bond with inverse_bond x 1.01", control_best > 1e-3,
            format("min |bond - 1| over the sets %.2e", control_best));
  }

  // The records. Own sets: the talk's defaults (rho 1: Lambert), the knob-bound sets and the named sets.
  const CallistoSet defaults = {1.f, 2.5f, 2.5f, 1.f, 2.5f, 2.5f, 1.f};
  const CallistoSet t3_control = {1.f + std::ldexp(1.f, -20), 2.5f, 2.5f, 1.f, 2.5f, 2.5f, 1.f};
  std::vector<CallistoSet> own_sets = {
      {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f},        // rho 0, every falloff 1 (exponents 0)
      {0.f, 10.f, 10.f, 0.f, 10.f, 10.f, 1.f},    // rho 0, every falloff 0 (exponents 10)
      {256.f, 0.f, 0.f, 256.f, 0.f, 0.f, 1.f},    // rho 256, falloffs 1
      {256.f, 10.f, 10.f, 256.f, 10.f, 10.f, 1.f},  // rho 256, falloffs 0
      {256.f, 0.f, 10.f, 0.f, 10.f, 0.f, 1.f},    // mixed bounds
      {0.f, 10.f, 0.f, 256.f, 0.f, 10.f, 1.f},
      defaults,
  };
  for (const NamedSet& named : NAMED_SETS) {
    own_sets.push_back(named.set);
  }
  // T4's edge records (n.l, n.v, l.v): the cosine and l.v sweeps, and l = -v, l = v (cos_d 1), h = n (cos_h 1), the
  // 0 / 0 record, back-facing and inconsistent triples.
  std::vector<std::array<float, 3>> edges;
  const std::array<float, 6> sweep_cosines = {-0.3f, 0.f, 1e-8f, 1e-4f, 0.1f, 1.f};
  for (const float n_dot_l : sweep_cosines) {
    for (const float n_dot_v : sweep_cosines) {
      for (const float l_dot_v : {-1.f, -1.f + 1e-7f, 0.f, 1.f}) {
        edges.push_back({n_dot_l, n_dot_v, l_dot_v});
      }
    }
  }
  for (const float u : {1e-4f, 0.1f, 0.5f, 0.9f, 1.f}) {
    edges.push_back({u, -u, -1.f});                     // l = -v
    edges.push_back({u, u, 1.f});                       // l = v
    edges.push_back({u, u, (2.f * u * u) - 1.f});       // h = n
    edges.push_back({1.f, u, u});                       // l = n
  }
  edges.push_back({0.f, 1e-8f, 1.f});                   // the 0 / 0 record (n.l 0)
  edges.push_back({1e-8f, 1e-8f, 1.f});
  edges.push_back({1.f, 1.f, -1.f});                    // inconsistent
  edges.push_back({0.1f, 0.1f, 1.f});
  edges.push_back({1e-4f, 1.f, -1.f});
  edges.push_back({1.f, 1e-4f, 0.9f});
  edges.push_back({0.5f, 0.5f, -1.f + 1e-7f});
  std::vector<ProbeInput> inputs;
  std::vector<RecordKind> kinds;
  const auto add = [&](RecordKind kind, const ProbeInput& input) {
    inputs.push_back(input);
    kinds.push_back(kind);
  };
  for (const std::array<float, 3>& edge : edges) {
    const auto& [n_dot_l, n_dot_v, l_dot_v] = edge;
    for (const CallistoSet& set : own_sets) {
      add(RecordKind::T3_ZERO, {.set = set, .n_dot_l = n_dot_l, .n_dot_v = n_dot_v, .l_dot_v = l_dot_v, .amount = 0.f});
      for (const float amount : {1.f, 0.5f}) {
        add(RecordKind::SWEEP,
            {.set = set, .n_dot_l = n_dot_l, .n_dot_v = n_dot_v, .l_dot_v = l_dot_v, .amount = amount});
      }
    }
    add(RecordKind::T3_ZERO, {.set = defaults, .n_dot_l = n_dot_l, .n_dot_v = n_dot_v, .l_dot_v = l_dot_v,
                              .amount = -1.f});
    add(RecordKind::T3_DEFAULTS,
        {.set = defaults, .n_dot_l = n_dot_l, .n_dot_v = n_dot_v, .l_dot_v = l_dot_v, .amount = 1.f});
    add(RecordKind::T3_CONTROL,
        {.set = t3_control, .n_dot_l = n_dot_l, .n_dot_v = n_dot_v, .l_dot_v = l_dot_v, .amount = 1.f});
  }
  // The talk's defaults and the control over consistent directions too (T3 holds everywhere, not only at the edges).
  for (int i = 1; i <= 10; ++i) {
    for (int j = 1; j <= 10; ++j) {
      for (int step = 0; step <= 6; ++step) {
        const float u_l = static_cast<float>(i) * 0.1f;
        const float u_v = static_cast<float>(j) * 0.1f;
        const float l_dot_v = LDotV(u_l, u_v, 30.0 * step);
        add(RecordKind::T3_DEFAULTS, {.set = defaults, .n_dot_l = u_l, .n_dot_v = u_v, .l_dot_v = l_dot_v,
                                      .amount = 1.f});
        add(RecordKind::T3_CONTROL, {.set = t3_control, .n_dot_l = u_l, .n_dot_v = u_v, .l_dot_v = l_dot_v,
                                     .amount = 1.f});
      }
    }
  }
  // The fade's ramp on consistent directions: n.v 0.03 / 0.05 / 0.08 against every n.l of the grid; the own set is
  // A + F (the first scoring set).
  for (const float n_dot_v : {0.03f, 0.05f, 0.08f}) {
    for (int i = 1; i <= 10; ++i) {
      for (int step = 0; step <= 6; ++step) {
        const float n_dot_l = static_cast<float>(i) * 0.1f;
        for (const float amount : {1.f, 0.5f}) {
          add(RecordKind::FADE, {.set = NAMED_SETS[PROBE_SETS].set, .n_dot_l = n_dot_l, .n_dot_v = n_dot_v,
                                 .l_dot_v = LDotV(n_dot_l, n_dot_v, 30.0 * step), .amount = amount});
        }
      }
    }
  }
  // The reciprocity grid, u 0.1 .. 1; its reciprocal swaps u_l and u_v.
  const size_t grid_start = inputs.size();
  const auto grid_index = [&](size_t i, size_t j, size_t step) { return grid_start + ((((i * 10) + j) * 7) + step); };
  for (size_t i = 0; i < 10; ++i) {
    for (size_t j = 0; j < 10; ++j) {
      for (size_t step = 0; step < 7; ++step) {
        const float u_l = static_cast<float>(i + 1) * 0.1f;
        const float u_v = static_cast<float>(j + 1) * 0.1f;
        add(RecordKind::GRID, {.set = own_sets[i % own_sets.size()], .n_dot_l = u_l, .n_dot_v = u_v,
                               .l_dot_v = LDotV(u_l, u_v, 30.0 * static_cast<double>(step)), .amount = 1.f});
      }
    }
  }
  while (inputs.size() % GROUP != 0) {
    add(RecordKind::PAD, {.set = defaults, .n_dot_l = 0.5f, .n_dot_v = 0.5f, .l_dot_v = 0.25f, .amount = 1.f});
  }
  // T-vmf's closed grid (the probe's `vmf`): u_l, u_v in {0.2, 0.205, ..., 1} (161), phi {0, 1, ..., 180} (181).
  std::vector<std::array<float, 3>> vmf_inputs;
  for (int i = 0; i <= 160; ++i) {
    for (int j = 0; j <= 160; ++j) {
      for (int step = 0; step <= 180; ++step) {
        const float u_l = 0.2f + (0.8f * static_cast<float>(i) / 160.f);
        const float u_v = 0.2f + (0.8f * static_cast<float>(j) / 160.f);
        vmf_inputs.push_back({u_l, u_v, LDotV(u_l, u_v, static_cast<double>(step))});
      }
    }
  }
  const size_t vmf_count = vmf_inputs.size();
  while (vmf_inputs.size() % GROUP != 0) {
    vmf_inputs.push_back({0.5f, 0.5f, 0.25f});
  }
  std::printf("records: %zu (%zu own sets x %zu edges, grid from %zu); T-vmf %zu\n", inputs.size(), own_sets.size(),
              edges.size(), grid_start, vmf_count);

  const auto twin_outputs = [&](const TwinOptions& options) {
    std::vector<ProbeOutput> outputs(inputs.size());
    for (size_t index = 0; index < inputs.size(); ++index) {
      const ProbeInput& input = inputs[index];
      for (size_t set = 0; set < PROBE_SETS; ++set) {
        outputs[index].named[set] =
            TwinLobe(NAMED_SETS[set].set, input.n_dot_l, input.n_dot_v, input.l_dot_v, input.amount, options);
      }
      outputs[index].own = TwinLobe(input.set, input.n_dot_l, input.n_dot_v, input.l_dot_v, input.amount, options);
    }
    return outputs;
  };
  // Every Callisto output of a record: the probe sets, then the own set.
  const auto lobes_of = [](const ProbeOutput& output) {
    std::array<float, PROBE_SETS + 1> lobes = {};
    std::copy(output.named.begin(), output.named.end(), lobes.begin());
    lobes[PROBE_SETS] = output.own;
    return lobes;
  };
  const std::vector<ProbeOutput> twin = twin_outputs({});
  const std::vector<ProbeOutput> unfaded = twin_outputs({.fade = false});

  // T2 on the twin's grid: reciprocity; and its controls (cos_h from n.l alone; the slide model's c2 at o 0.25).
  const auto reciprocity = [&](const std::vector<ProbeOutput>& outputs) {
    double worst = 0;
    for (size_t i = 0; i < 10; ++i) {
      for (size_t j = 0; j < 10; ++j) {
        for (size_t step = 0; step < 7; ++step) {
          const auto forward = lobes_of(outputs[grid_index(i, j, step)]);
          const auto backward = lobes_of(outputs[grid_index(j, i, step)]);
          for (size_t field = 0; field < PROBE_SETS; ++field) {  // own sets differ between i and j
            worst = std::max(worst, relative(forward[field], backward[field]));
          }
        }
      }
    }
    return worst;
  };
  {
    const double from_n_dot_l = reciprocity(twin_outputs({.cos_h_from_n_dot_l = true}));
    control("T2 twin with cos_h from n.l alone", from_n_dot_l > 1e-6, format("max relative %.2e", from_n_dot_l));
    const SlideModel* terminator = nullptr;
    for (const SlideModel& model : SLIDE_MODELS) {
      if (std::string(model.name) == "c2 o 0.25 radians") {
        terminator = &model;
      }
    }
    if (terminator == nullptr) return setup_failed("no \"c2 o 0.25 radians\" model in slide_points.h");
    double worst = 0;
    for (int i = 1; i <= 10; ++i) {
      for (int j = 1; j <= 10; ++j) {
        for (int step = 0; step <= 6; ++step) {
          const Vector l = Direction(std::acos(0.1 * i) / DEGREE, 0.0);
          const Vector v = Direction(std::acos(0.1 * j) / DEGREE, 30.0 * step);
          worst = std::max(worst, relative(SlideLobe(*terminator, l, v, {}), SlideLobe(*terminator, v, l, {})));
        }
      }
    }
    control("T2 the slide model's c2 at o 0.25 (radians), u >= 0.1", worst > 1e-6, format("max relative %.2e", worst));
  }

  // T4's NaN control: neither the exponent-0 fold nor the base clamp, at h = n (n_r 1: l = v = n) and at l = v (n_f 1);
  // either guard alone keeps them finite (IEEE).
  {
    const CallistoSet retro_bound = {3.f, 0.f, 2.5f, 1.f, 2.5f, 2.5f, 1.f};
    const CallistoSet fresnel_bound = {1.f, 2.5f, 2.5f, 25.f, 0.f, 0.3f, 1.f};
    const std::array<std::pair<const char*, TwinOptions>, 3> guards = {{
        {"neither guard", {.fold = false, .clamp_base = false}},
        {"the fold alone", {.clamp_base = false}},
        {"the clamp alone", {.fold = false}},
    }};
    std::array<std::string, 3> rows;
    std::array<bool, 3> finite = {};
    bool both_nan = false;
    for (size_t guard = 0; guard < guards.size(); ++guard) {
      const float at_h_n = TwinLobe(retro_bound, 1.f, 1.f, 1.f, 1.f, guards[guard].second);
      const float at_l_v = TwinLobe(fresnel_bound, 0.6f, 0.6f, 1.f, 1.f, guards[guard].second);
      finite[guard] = std::isfinite(at_h_n) && std::isfinite(at_l_v);
      if (guard == 0) {
        both_nan = !std::isfinite(at_h_n) && !std::isfinite(at_l_v);
      }
      rows[guard] = format("%s: h = n %g, l = v %g", guards[guard].first, at_h_n, at_l_v);
    }
    control("twin T4 finite with " + rows[0], both_nan, "both NaN required");
    gate("twin T4 finite with either guard alone", finite[1] && finite[2], rows[1] + "; " + rows[2]);
  }

  // T3 / T4 / the fade / reciprocity on one build's outputs (the twin's or a WARP build's); `fade_reference` is the
  // unfaded twin.
  const auto check_build = [&](const std::string& build, const std::vector<ProbeOutput>& outputs) {
    bool t3_ok = true;
    bool control_all_one = true;
    bool finite = true;
    bool in_range = true;
    bool outside_one = true;
    double fade_worst = 0;
    for (size_t index = 0; index < inputs.size(); ++index) {
      const ProbeInput& input = inputs[index];
      const auto lobes = lobes_of(outputs[index]);
      const auto all_one = [&]() {
        return std::all_of(lobes.begin(), lobes.end(),
                           [](float value) { return std::bit_cast<uint32_t>(value) == ONE_BITS; });
      };
      if (kinds[index] == RecordKind::PAD) continue;
      finite = finite && std::all_of(lobes.begin(), lobes.end(), [](float value) { return std::isfinite(value); });
      in_range = in_range && std::all_of(lobes.begin(), lobes.end(),
                                         [](float value) { return value >= 0.f && value <= DESIGN_LOBE_MAX; });
      switch (kinds[index]) {
        case RecordKind::T3_ZERO:
          t3_ok = t3_ok && all_one();
          break;
        case RecordKind::T3_DEFAULTS:
          t3_ok = t3_ok && std::bit_cast<uint32_t>(outputs[index].own) == ONE_BITS;
          break;
        case RecordKind::T3_CONTROL:
          control_all_one = control_all_one && std::bit_cast<uint32_t>(outputs[index].own) == ONE_BITS;
          break;
        case RecordKind::FADE: {
          const auto reference = lobes_of(unfaded[index]);
          for (size_t field = 0; field < lobes.size(); ++field) {
            const double weight = input.n_dot_v / DESIGN_FADE_COSINE;
            const double expected = 1.0 + (weight * (reference[field] - 1.0));
            fade_worst = std::max(fade_worst, std::abs(lobes[field] - expected) / std::max(expected, 1.0));
          }
          break;
        }
        default:
          break;
      }
      if (input.n_dot_l <= 0.f || input.n_dot_v <= 0.f) {
        outside_one = outside_one && all_one();
      }
    }
    const double reciprocity_worst = reciprocity(outputs);
    gate(build + " T3 amount 0, and the talk's defaults at amount 1: exactly 1.0f", t3_ok,
         "every set at amount 0 (and -1); the defaults' own output at amount 1");
    control(build + " T3 with rho_r 1 + 2^-20", !control_all_one,
            control_all_one ? "all exactly 1" : "R != 1.0f somewhere");
    gate(build + " T4 finite", finite, "every Callisto output of every record");
    gate(build + " T4 R in [0, 4]", in_range, "every Callisto output of every record (bound sets: rho 0 / 256)");
    gate(build + " T4 R exactly 1 at n.l <= 0 and n.v <= 0", outside_one, "every set, every amount");
    gate(build + " T4 fade: R = 1 + (n.v / 0.1) (unfaded twin - 1) below n.v 0.1", fade_worst <= SAME_CODE,
         format("max %.2e (<= 1e-4 x max(R, 1)), n.v 0.03 / 0.05 / 0.08", fade_worst));
    gate(build + " T2 reciprocity of c1, u >= 0.1", reciprocity_worst <= 1e-6,
         format("max relative %.2e (<= 1e-6)", reciprocity_worst));
  };
  check_build("twin", twin);
  {
    const std::vector<ProbeOutput> early = twin_outputs({.fade_cosine = 0.08f});
    double worst = 0;
    for (size_t index = 0; index < inputs.size(); ++index) {
      if (kinds[index] != RecordKind::FADE) continue;
      const auto lobes = lobes_of(early[index]);
      const auto reference = lobes_of(unfaded[index]);
      for (size_t field = 0; field < lobes.size(); ++field) {
        const double weight = inputs[index].n_dot_v / DESIGN_FADE_COSINE;
        const double expected = 1.0 + (weight * (reference[field] - 1.0));
        worst = std::max(worst, std::abs(lobes[field] - expected) / std::max(expected, 1.0));
      }
    }
    control("twin T4 fade with the fade at 0.08", worst > SAME_CODE, format("max %.2e", worst));
  }

  // WARP against the twin (the same code: 1e-4 x max(|twin|, 1) per output), and against the control's twin.
  const std::vector<ProbeOutput> swapped = twin_outputs({.swap_exponents = true});
  const auto difference = [&](const std::vector<ProbeOutput>& outputs, const std::vector<ProbeOutput>& reference,
                              std::string* where) {
    double worst = 0;
    for (size_t index = 0; index < inputs.size(); ++index) {
      const auto measured = lobes_of(outputs[index]);
      const auto expected = lobes_of(reference[index]);
      for (size_t field = 0; field < measured.size(); ++field) {
        const double gap = std::abs(static_cast<double>(measured[field]) - expected[field]);
        const double error = std::isfinite(gap) ? gap / std::max(std::abs(static_cast<double>(expected[field])), 1.0)
                                                : std::numeric_limits<double>::infinity();
        if (error > worst) {
          worst = error;
          const ProbeInput& input = inputs[index];
          *where = format("record %zu (kind %d) output %zu: %.7g vs %.7g at n.l %g, n.v %g, l.v %g, amount %g", index,
                          static_cast<int>(kinds[index]), field, measured[field], expected[field], input.n_dot_l,
                          input.n_dot_v, input.l_dot_v, input.amount);
        }
      }
    }
    return worst;
  };
  // T-vmf on a WARP build's VmfLobe outputs ({r 0.16, r 0.40} per vmf_inputs record): V16 / V40 (twin) and Lambert
  // (the control).
  const auto vmf_check = [&](const std::string& build, const std::vector<std::array<float, 2>>& vmf_outputs) {
    // weighted (u_l u_v) rms and max of lobe / vMF - 1 for `set` (nullptr: Lambert) against r 0.16 or 0.40; where the
    // max is.
    const auto score = [&](const CallistoSet* set, size_t roughness, std::string* where) {
      double squares = 0;
      double weights = 0;
      double worst = 0;
      for (size_t index = 0; index < vmf_count; ++index) {
        const auto& [n_dot_l, n_dot_v, l_dot_v] = vmf_inputs[index];
        const double lobe = (set == nullptr) ? 1.0 : TwinLobe(*set, n_dot_l, n_dot_v, l_dot_v, 1.f, {});
        const double error = relative(lobe, vmf_outputs[index][roughness]);
        const double weight = static_cast<double>(n_dot_l) * n_dot_v;
        squares += weight * error * error;
        weights += weight;
        if (error > worst) {
          worst = error;
          *where = format("u_l %.3f u_v %.3f phi %zu", n_dot_l, n_dot_v, index % 181);
        }
      }
      return std::array<double, 2>{std::sqrt(squares / weights), worst};
    };
    std::string v16_where;
    std::string v40_where;
    std::string unused;
    const std::array<double, 2> v16 = score(&CALLISTO_SET_V16, 0, &v16_where);
    const std::array<double, 2> v40 = score(&CALLISTO_SET_V40, 1, &v40_where);
    gate(build + " T-vmf V16 / V40 vs VmfLobe r 0.16 / 0.40, base 0.5, closed grid u 0.2..1 (161) x phi 0..180 (181)",
         v16[0] <= 0.02 && v16[1] <= 0.40 && v40[0] <= 0.02 && v40[1] <= 0.20,
         format("V16 rms %.2f %% / max %.1f %% at %s (<= 2 / 40); V40 %.2f %% / %.1f %% at %s (<= 2 / 20)",
                100 * v16[0], 100 * v16[1], v16_where.c_str(), 100 * v40[0], 100 * v40[1], v40_where.c_str()));
    const std::array<double, 2> lambert_16 = score(nullptr, 0, &unused);
    const std::array<double, 2> lambert_40 = score(nullptr, 1, &unused);
    control(build + " T-vmf with Lambert", lambert_16[0] > 0.02 && lambert_40[0] > 0.02,
            format("rms %.2f %% (r 0.16) / %.2f %% (r 0.40)", 100 * lambert_16[0], 100 * lambert_40[0]));
  };
  const auto compare_with_twin = [&](const std::string& build, const std::vector<ProbeOutput>& outputs) {
    check_build(build, outputs);
    std::string where;
    const double worst = difference(outputs, twin, &where);
    gate(build + " against the twin, <= 1e-4 x max(|twin|, 1)", worst <= SAME_CODE,
         format("max %.2e, %s", worst, where.c_str()));
    const double control_worst = difference(outputs, swapped, &where);
    control(build + " against the twin with the retro exponents swapped", control_worst > SAME_CODE,
            format("max %.2e", control_worst));
  };

  const std::filesystem::path probe_path =
      std::filesystem::path(repository) / "test" / "tw_callisto_diffuse" / "callisto_probe.hlsl";
  // The cost line: the probe's `cost` per named set (CALLISTO_COST_KNOBS = its seven floats, exact in %.8e) against
  // `cost_base` (lobe = cost - base + 2: the base has two more multiplies than the lobe's caller).
  std::array<std::string, NAMED_SETS.size()> cost_knobs;
  for (size_t set = 0; set < NAMED_SETS.size(); ++set) {
    const CallistoSet& knobs = NAMED_SETS[set].set;
    cost_knobs[set] = format("%.8ef, %.8ef, %.8ef, %.8ef, %.8ef, %.8ef, %.8ef", knobs.retro, knobs.retro_exponent,
                             knobs.retro_tangent_exponent, knobs.fresnel, knobs.fresnel_exponent,
                             knobs.fresnel_tangent_exponent, knobs.inverse_bond);
  }

  // fxc (cs_5_0, /O3 as the add-ons' CMake) on D3D11 WARP.
  {
    std::string cost_line;
    std::string static_problems;
    // The probe's `entry` with CALLISTO_COST_KNOBS = `knobs` (or none), its static gates folded into one line; the
    // slot count through `slots`.
    const auto compile = [&](const char* entry, const char* knobs, int* slots) {
      ComPtr<ID3DBlob> code;
      ComPtr<ID3DBlob> messages;
      IncludeBesideParent includes(probe_path.parent_path());
      const std::array<D3D_SHADER_MACRO, 2> macros = {{{"CALLISTO_COST_KNOBS", knobs}, {nullptr, nullptr}}};
      const HRESULT compiled = D3DCompileFromFile(probe_path.c_str(), (knobs != nullptr) ? macros.data() : nullptr,
                                                  &includes, entry, "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                                                  code.GetAddressOf(), messages.GetAddressOf());
      const std::string message_text =
          (messages != nullptr) ? std::string(static_cast<const char*>(messages->GetBufferPointer())) : "";
      const char* set_name = (knobs != nullptr) ? knobs : "";
      if (FAILED(compiled) || !message_text.empty()) {
        static_problems += format("%s %s: %s ", entry, set_name, message_text.c_str());
      }
      if (FAILED(compiled)) return ComPtr<ID3DBlob>();
      ComPtr<ID3DBlob> listing;
      if (FAILED(D3DDisassemble(code->GetBufferPointer(), code->GetBufferSize(), 0, nullptr, listing.GetAddressOf()))) {
        static_problems += "disassembly ";
        return ComPtr<ID3DBlob>();
      }
      const std::string assembly(static_cast<const char*>(listing->GetBufferPointer()));
      if (assembly.find("dcl_indexableTemp") != std::string::npos
          || assembly.find("dcl_immediateConstantBuffer") != std::string::npos) {
        static_problems += format("%s %s: indexable temp or icb ", entry, set_name);
      }
      const size_t at = assembly.find("// Approximately ");
      *slots = (at != std::string::npos) ? std::atoi(assembly.c_str() + at + 17) : -1;
      return code;
    };
    int main_slots = 0;
    const ComPtr<ID3DBlob> code = compile("main", nullptr, &main_slots);
    int vmf_slots = 0;
    const ComPtr<ID3DBlob> vmf_code = compile("vmf", nullptr, &vmf_slots);
    int base_slots = 0;
    compile("cost_base", cost_knobs[0].c_str(), &base_slots);
    for (size_t set = 0; set < cost_knobs.size(); ++set) {
      int slots = 0;
      compile("cost", cost_knobs[set].c_str(), &slots);
      cost_line += format(" %s %d;", NAMED_SETS[set].name, slots - base_slots + 2);
    }
    gate("static fxc cs_5_0 /O3: no warnings, no indexable temp, no immediate constant buffer",
         static_problems.empty(), static_problems.empty() ? format("clean (main %d slots)", main_slots)
                                                          : static_problems);
    std::printf("COST fxc cs_5_0 instruction slots per lobe (cost - base + 2; base %d):%s\n", base_slots,
                cost_line.c_str());
    if (code == nullptr || vmf_code == nullptr) return setup_failed("fxc compile");

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                 device.GetAddressOf(), nullptr, context.GetAddressOf()))) {
      return setup_failed("no D3D11 WARP device");
    }
    // One dispatch of `shader_code` over `count` records of `input_stride` bytes at t`slot`, its outputs of
    // `output_stride` bytes from u`slot`; empty on failure (the step named).
    const auto run = [&](ID3DBlob* shader_code, const void* records, size_t count, UINT input_stride,
                         UINT output_stride, UINT slot) {
      std::vector<uint8_t> result;
      ComPtr<ID3D11ComputeShader> shader;
      const D3D11_BUFFER_DESC input_desc = {.ByteWidth = static_cast<UINT>(count * input_stride),
                                            .Usage = D3D11_USAGE_DEFAULT,
                                            .BindFlags = D3D11_BIND_SHADER_RESOURCE,
                                            .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
                                            .StructureByteStride = input_stride};
      const D3D11_SUBRESOURCE_DATA input_data = {.pSysMem = records};
      ComPtr<ID3D11Buffer> input_buffer;
      D3D11_BUFFER_DESC output_desc = {.ByteWidth = static_cast<UINT>(count * output_stride),
                                       .Usage = D3D11_USAGE_DEFAULT,
                                       .BindFlags = D3D11_BIND_UNORDERED_ACCESS,
                                       .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
                                       .StructureByteStride = output_stride};
      ComPtr<ID3D11Buffer> output_buffer;
      const UINT elements = static_cast<UINT>(count);
      const D3D11_SHADER_RESOURCE_VIEW_DESC input_view_desc = {.Format = DXGI_FORMAT_UNKNOWN,
                                                               .ViewDimension = D3D11_SRV_DIMENSION_BUFFER,
                                                               .Buffer = {.FirstElement = 0, .NumElements = elements}};
      const D3D11_UNORDERED_ACCESS_VIEW_DESC output_view_desc = {
          .Format = DXGI_FORMAT_UNKNOWN,
          .ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
          .Buffer = {.FirstElement = 0, .NumElements = elements}};
      ComPtr<ID3D11ShaderResourceView> input_view;
      ComPtr<ID3D11UnorderedAccessView> output_view;
      ComPtr<ID3D11Buffer> staging;
      if (FAILED(device->CreateComputeShader(shader_code->GetBufferPointer(), shader_code->GetBufferSize(), nullptr,
                                             shader.GetAddressOf()))
          || FAILED(device->CreateBuffer(&input_desc, &input_data, input_buffer.GetAddressOf()))
          || FAILED(device->CreateBuffer(&output_desc, nullptr, output_buffer.GetAddressOf()))
          || FAILED(device->CreateShaderResourceView(input_buffer.Get(), &input_view_desc, input_view.GetAddressOf()))
          || FAILED(device->CreateUnorderedAccessView(output_buffer.Get(), &output_view_desc,
                                                      output_view.GetAddressOf()))) {
        std::printf("FAIL setup: D3D11 shader, buffers or views\n");
        return result;
      }
      output_desc.Usage = D3D11_USAGE_STAGING;
      output_desc.BindFlags = 0;
      output_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      if (FAILED(device->CreateBuffer(&output_desc, nullptr, staging.GetAddressOf()))) {
        std::printf("FAIL setup: D3D11 staging buffer\n");
        return result;
      }
      context->CSSetShader(shader.Get(), nullptr, 0);
      context->CSSetShaderResources(slot, 1, input_view.GetAddressOf());
      context->CSSetUnorderedAccessViews(slot, 1, output_view.GetAddressOf(), nullptr);
      context->Dispatch(static_cast<UINT>(count / GROUP), 1, 1);
      context->CopyResource(staging.Get(), output_buffer.Get());
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        std::printf("FAIL setup: D3D11 readback map\n");
        return result;
      }
      result.resize(count * output_stride);
      std::memcpy(result.data(), mapped.pData, result.size());
      context->Unmap(staging.Get(), 0);
      return result;
    };
    const std::vector<uint8_t> bytes =
        run(code.Get(), inputs.data(), inputs.size(), sizeof(ProbeInput), sizeof(ProbeOutput), 0);
    if (bytes.empty()) return 1;
    std::vector<ProbeOutput> outputs(inputs.size());
    std::memcpy(outputs.data(), bytes.data(), bytes.size());
    compare_with_twin("fxc/D3D11 WARP", outputs);
    const std::vector<uint8_t> vmf_bytes = run(vmf_code.Get(), vmf_inputs.data(), vmf_inputs.size(),
                                               sizeof(vmf_inputs[0]), sizeof(std::array<float, 2>), 1);
    if (vmf_bytes.empty()) return 1;
    std::vector<std::array<float, 2>> vmf_outputs(vmf_inputs.size());
    std::memcpy(vmf_outputs.data(), vmf_bytes.data(), vmf_bytes.size());
    vmf_check("fxc/D3D11 WARP", vmf_outputs);
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
    std::string static_problems;
    // The probe's `entry` at `target` with CALLISTO_COST_KNOBS = `knobs` (or none), its static gates folded into one
    // line; its entry's DXIL instruction count through `instructions` (the function body's indented lines).
    const auto compile = [&](const wchar_t* target, const wchar_t* entry, const std::string* knobs,
                             int* instructions) {
      std::wstring define = L"CALLISTO_COST_KNOBS=";
      std::vector<LPCWSTR> arguments = {probe_path.c_str(), L"-T", target, L"-E", entry, L"-O3"};
      if (knobs != nullptr) {
        define.append(knobs->begin(), knobs->end());  // ASCII
        arguments.push_back(L"-D");
        arguments.push_back(define.c_str());
      }
      const std::string name = format("%ls %ls %s", target, entry, (knobs != nullptr) ? knobs->c_str() : "");
      ComPtr<IDxcResult> result;
      HRESULT status = E_FAIL;
      ComPtr<IDxcBlob> object;
      if (FAILED(compiler->Compile(&source_buffer, arguments.data(), static_cast<UINT32>(arguments.size()),
                                   includes.Get(), IID_PPV_ARGS(&result)))
          || FAILED(result->GetStatus(&status))) {
        static_problems += name + ": compile call ";
        return object;
      }
      ComPtr<IDxcBlobUtf8> errors;
      result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
      if (errors != nullptr && errors->GetStringLength() > 0) {
        static_problems += name + ": " + errors->GetStringPointer() + " ";
      }
      if (FAILED(status)) return object;
      result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr);
      const DxcBuffer object_buffer = {.Ptr = object->GetBufferPointer(), .Size = object->GetBufferSize()};
      ComPtr<IDxcResult> disassembled;
      ComPtr<IDxcBlobUtf8> listing;
      if (FAILED(compiler->Disassemble(&object_buffer, IID_PPV_ARGS(&disassembled)))
          || FAILED(disassembled->GetOutput(DXC_OUT_DISASSEMBLY, IID_PPV_ARGS(&listing), nullptr))) {
        static_problems += name + ": disassembly ";
        return ComPtr<IDxcBlob>();
      }
      const std::string assembly = listing->GetStringPointer();
      if (assembly.find("alloca") != std::string::npos || assembly.find("dx.op.wave") != std::string::npos
          || assembly.find(" half ") != std::string::npos) {
        static_problems += name + ": alloca, wave op or half ";
      }
      const size_t body = assembly.find(format("define void @%ls()", entry));
      *instructions = 0;
      for (size_t line = assembly.find('\n', body);
           line != std::string::npos && assembly.compare(line, 3, "\n}\n") != 0; line = assembly.find('\n', line + 1)) {
        if (assembly.compare(line + 1, 2, "  ") == 0 && assembly[line + 3] != ';') {
          ++*instructions;
        }
      }
      return object;
    };
    int main_instructions = 0;
    if (compile(L"cs_6_9", L"main", nullptr, &main_instructions) == nullptr) return setup_failed("dxc cs_6_9");
    const ComPtr<IDxcBlob> object = compile(L"cs_6_5", L"main", nullptr, &main_instructions);
    int vmf_instructions = 0;
    const ComPtr<IDxcBlob> vmf_object = compile(L"cs_6_5", L"vmf", nullptr, &vmf_instructions);
    if (object == nullptr || vmf_object == nullptr) return setup_failed("dxc cs_6_5");
    int base_instructions = 0;
    compile(L"cs_6_5", L"cost_base", cost_knobs.data(), &base_instructions);
    std::string cost_line;
    for (size_t set = 0; set < cost_knobs.size(); ++set) {
      int instructions = 0;
      compile(L"cs_6_5", L"cost", &cost_knobs[set], &instructions);
      cost_line += format(" %s %d;", NAMED_SETS[set].name, instructions - base_instructions + 2);
    }
    gate("static dxc cs_6_5 / cs_6_9 -O3: no warnings, no alloca, no wave op, no half", static_problems.empty(),
         static_problems.empty()
             ? format("clean (main %d DXIL instructions, %zu bytes)", main_instructions,
                      static_cast<size_t>(object->GetBufferSize()))
             : static_problems);
    std::printf("COST dxc cs_6_5 DXIL instructions per lobe (cost - base + 2; base %d):%s\n", base_instructions,
                cost_line.c_str());

    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter> warp;
    ComPtr<ID3D12Device> device;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)))
        || FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) {
      return setup_failed("no D3D12 WARP device");
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
    // One dispatch of `shader_object` over `count` records of `input_stride` bytes at t`slot` (root SRV), its outputs
    // of `output_stride` bytes from u`slot` (root UAV); empty on failure (the step named).
    const auto run = [&](IDxcBlob* shader_object, const void* records, size_t count, size_t input_stride,
                         size_t output_stride, UINT slot) {
      std::vector<uint8_t> result;
      const std::array<D3D12_ROOT_PARAMETER, 2> parameters = {{
          {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV, .Descriptor = {.ShaderRegister = slot, .RegisterSpace = 0}},
          {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV, .Descriptor = {.ShaderRegister = slot, .RegisterSpace = 0}},
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
        std::printf("FAIL setup: D3D12 root signature\n");
        return result;
      }
      const D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {
          .pRootSignature = root.Get(),
          .CS = {.pShaderBytecode = shader_object->GetBufferPointer(),
                 .BytecodeLength = shader_object->GetBufferSize()}};
      if (FAILED(device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&pipeline)))) {
        std::printf("FAIL setup: D3D12 pipeline\n");
        return result;
      }
      const size_t input_size = count * input_stride;
      const size_t output_size = count * output_stride;
      const ComPtr<ID3D12Resource> upload =
          buffer(input_size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
      const ComPtr<ID3D12Resource> output = buffer(output_size, D3D12_HEAP_TYPE_DEFAULT,
                                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                   D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
      const ComPtr<ID3D12Resource> readback =
          buffer(output_size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE);
      void* mapped = nullptr;
      if (upload == nullptr || output == nullptr || readback == nullptr || FAILED(upload->Map(0, nullptr, &mapped))) {
        std::printf("FAIL setup: D3D12 buffers\n");
        return result;
      }
      std::memcpy(mapped, records, input_size);
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
        std::printf("FAIL setup: D3D12 queue, allocator, list or fence\n");
        return result;
      }
      list->SetComputeRootSignature(root.Get());
      list->SetComputeRootShaderResourceView(0, upload->GetGPUVirtualAddress());
      list->SetComputeRootUnorderedAccessView(1, output->GetGPUVirtualAddress());
      list->Dispatch(static_cast<UINT>(count / GROUP), 1, 1);
      const D3D12_RESOURCE_BARRIER barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                                              .Transition = {.pResource = output.Get(),
                                                             .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                                             .StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                             .StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE}};
      list->ResourceBarrier(1, &barrier);
      list->CopyResource(readback.Get(), output.Get());
      if (FAILED(list->Close())) {
        std::printf("FAIL setup: D3D12 list close\n");
        return result;
      }
      ID3D12CommandList* lists[] = {list.Get()};
      queue->ExecuteCommandLists(1, lists);
      if (FAILED(queue->Signal(fence.Get(), 1)) || FAILED(fence->SetEventOnCompletion(1, nullptr))
          || FAILED(device->GetDeviceRemovedReason())) {
        std::printf("FAIL setup: D3D12 fence wait or WARP device removed\n");
        return result;
      }
      const D3D12_RANGE read_range = {.Begin = 0, .End = output_size};
      if (FAILED(readback->Map(0, &read_range, &mapped))) {
        std::printf("FAIL setup: D3D12 readback map\n");
        return result;
      }
      result.resize(output_size);
      std::memcpy(result.data(), mapped, output_size);
      const D3D12_RANGE written = {.Begin = 0, .End = 0};
      readback->Unmap(0, &written);
      return result;
    };
    const std::vector<uint8_t> bytes =
        run(object.Get(), inputs.data(), inputs.size(), sizeof(ProbeInput), sizeof(ProbeOutput), 0);
    if (bytes.empty()) return 1;
    std::vector<ProbeOutput> outputs(inputs.size());
    std::memcpy(outputs.data(), bytes.data(), bytes.size());
    compare_with_twin("dxc/D3D12 WARP", outputs);
    const std::vector<uint8_t> vmf_bytes = run(vmf_object.Get(), vmf_inputs.data(), vmf_inputs.size(),
                                               sizeof(vmf_inputs[0]), sizeof(std::array<float, 2>), 1);
    if (vmf_bytes.empty()) return 1;
    std::vector<std::array<float, 2>> vmf_outputs(vmf_inputs.size());
    std::memcpy(vmf_outputs.data(), vmf_bytes.data(), vmf_bytes.size());
    vmf_check("dxc/D3D12 WARP", vmf_outputs);
  }

  const bool all_ok = gates_failed == 0 && controls_passed == 0;
  std::printf("\n%s: %d gate(s) failed, %d control(s) passed that must fail\n", all_ok ? "ALL OK" : "NOT OK",
              gates_failed, controls_passed);
  return all_ok ? 0 : 1;
}
