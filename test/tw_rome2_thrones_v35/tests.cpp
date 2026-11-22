// PsychoV35's V35 FIT for Rome II and Thrones of Britannia on WARP (the recipe: Shogun 2's test/tw_shogun2_romer_toe,
// tools\tw\testbin\scratch\g04_\v35_pilot.md), through test/tw_psychov35's chains (its tests.cpp included, its main
// renamed): Rome II's t00 EE1DA45C -> t01 850AEE44 (black 10^-4, white 1, the identity curve), Thrones' t00 8BF6B657 ->
// t01 D0FAF089 (the identity S-curve), both decoded in the frame's gamma (Gamma Correction on: 2.2). PsychoV35 at each
// game's Hue Clip default (Rome II 80, Thrones 40), 1000 / 203 nits, against Rendering Mode SDR.
// Both games' PsychoV35 sits after the game's own curve, so its grey below white already is the game's. Rome II's SDR
// also raises black (t00: max(0.011) per channel before its curve, 1.4 nits here), a floor no V35 slider makes: the
// measure leaves out the rows where SDR is still at its floor (within 1 % of its darkest row). Rome II's colour is not
// the game's (x1.23 at 1 - 30 nits): its SDR curve runs on the xyY luminance after that floor.
// The fit: PsychoV35's Contrast, Shadows, Saturation and Highlight Contrast minimax-fitted to the game's SDR below
// white: the worst of grey |V35 / SDR - 1| where SDR is 0.05 - 1 and 1 - 15 nits and |chroma - 1| (OkLab at equal
// lightness, the geometric mean) where SDR is 1 - 30 nits; a candidate counts only if it keeps PsychoV35's highlights
// at 1000 / 203 (grey within a tolerance of Musa's own where Musa's own is 100 nits up to 95 % of Peak): 10, 15 and 20 %
// fitted, the smallest whose best is under 5 % shipped (v35_pilot.md's rule). Coarse to fine: every 0.1, then 0.05 and
// 0.01 around the best. Checks per game: the add-on's defaults (Rome II shared.h ROME2_V35_*; Thrones Musa's own, its
// fit's best) within 5 % and within 0.5 % of that fit's best, its guard at 1000 / 203, 2500 / 275 within 20 %; CONTROL
// that must fail 5 %: Contrast and Saturation 1.2; the grey ramps rise, SDR reaches 100 nits, the measured bands are not
// empty. Report: Rome II on the S-shaped curve (a scene the fit did not see), before and after.
// tests <cso folder> (run.ps1: r_t00, r_t01, th_t00, th_t01 with the build's flags, reflection kept).
#include <chrono>

#define main psychov35_main
#include "test/tw_psychov35/tests.cpp"
#undef main

using Values = std::array<float, 4>;  // Contrast, Shadows, Saturation, Highlight Contrast
constexpr Values MUSA = {1.f, 1.f, 1.f, 1.f};
constexpr float NITS = 203.f;
constexpr float PEAK = 1000.f;

// 2^-14 .. 2^7.15 of the game's white (0.45 stops a row): SDR's 0.05 nits up to PsychoV35's Peak.
static std::vector<Float4> FitScene() {
  std::vector<Float3> columns = {{1.f, 1.f, 1.f}};
  for (int h = 0; h < 21; ++h) {
    for (const float s : {0.4f, 0.8f, 0.95f}) columns.push_back(Hue(h * 360.f / 21.f, s));
  }
  return Scene(columns, -14.f, 0.45f, 1.f);
}

// Rome II's chain with the add-on's data (RunRome2's bindings; its sliders are fixed at Musa's own).
static rome2::ShaderInjectData Rome2Injection(float mode, float hue_clip) {
  rome2::ShaderInjectData si = {};
  si.peak_white_nits = PEAK;
  si.diffuse_white_nits = NITS;
  si.graphics_white_nits = NITS;
  si.gamma_correction = 1.f;
  si.swap_chain_encoding = 1.f;
  si.hdr = mode;
  si.bloom = 1.f;
  si.vignette = 1.f;
  si.godrays = 1.f;
  si.v35_exposure = si.v35_highlights = si.v35_shadows = si.v35_contrast = 1.f;
  si.v35_highlight_contrast = si.v35_shadow_contrast = 1.f;
  si.v35_saturation = si.v35_highlight_saturation = 1.f;
  si.v35_roll_off = 1.f;
  si.v35_gamut_compression = 1.f;
  si.hue_clip = hue_clip;
  return si;
}

static std::vector<Float3> RunRome2(const Chain& chain, const std::vector<Float4>& scene, const rome2::ShaderInjectData& si,
                                    bool identity_curve = true) {
  Bindings first = AttilaToneMapper(scene, identity_curve);
  first.views["g_black_and_white_points_sampler"] = Texture2D(1, 1, {Float4{-4.f, 1e-4f, 0.f, 1.f}});
  first.injection = Bytes(si);
  const auto handoff = Draw(chain.rome2_tone_mapper->after, first);
  Bindings second = AttilaHdrToScreen(handoff, true);
  second.views["god_rays_sampler"] = Texture2D(WIDTH, HEIGHT, std::vector<Float4>(WIDTH * HEIGHT, Float4{}));
  second.words["g_gamma_output"] = F({2.2f});
  second.words["g_inv_gamma_output"] = F({1.f / 2.2f});
  second.injection = Bytes(si);
  std::vector<Float3> linear;
  for (const auto& o : Draw(chain.rome2_hdr_to_screen->after, second)) linear.push_back(Decode(o, 2.2f));
  return linear;
}

template <typename Injection>
static Injection WithSliders(Injection si, const Values& values) {
  si.v35_contrast = values[0];
  si.v35_shadows = values[1];
  si.v35_saturation = values[2];
  si.v35_highlight_contrast = values[3];
  return si;
}

struct Range {
  float low = 1e9f, high = 0.f;
  int rows = 0;
};

// PsychoV35 over SDR above SDR's black floor: grey where SDR is 0.05 - 1, 1 - 15 and 15 - 100 nits; colour (OkLab chroma
// at equal lightness, the geometric mean over the 63 colours) where SDR is 1 - 30 and 30 - 150 nits.
struct Comparison {
  std::array<Range, 3> grey;
  std::array<float, 2> chroma = {};
  std::array<int, 2> chroma_counts = {};
  float floor_nits = 0.f;  // SDR's darkest grey
};

static Comparison Compare(const std::vector<Float3>& sdr, const std::vector<Float3>& v35) {
  Comparison comparison;
  comparison.floor_nits = Luminance(sdr[0]) * NITS;
  const auto above_floor = [&](float nits) { return nits > 1.01f * comparison.floor_nits; };
  for (UINT y = 0; y < HEIGHT; ++y) {
    const float sdr_nits = Luminance(sdr[y * WIDTH]) * NITS;
    const float ratio = Luminance(v35[y * WIDTH]) / std::max(Luminance(sdr[y * WIDTH]), 1e-9f);
    const int band = (sdr_nits >= 0.05f && sdr_nits <= 1.f) ? 0 : ((sdr_nits > 1.f && sdr_nits <= 15.f) ? 1 : ((sdr_nits > 15.f && sdr_nits <= 100.f) ? 2 : -1));
    if (band < 0 || !above_floor(sdr_nits)) continue;
    auto& range = comparison.grey[band];
    range.low = std::min(range.low, ratio);
    range.high = std::max(range.high, ratio);
    ++range.rows;
  }
  std::array<double, 2> log_sums = {};
  for (UINT column = 1; column < WIDTH; ++column) {
    const float column_floor = Luminance(sdr[column]) * NITS;
    for (UINT y = 0; y < HEIGHT; ++y) {
      const Float3& game_colour = sdr[y * WIDTH + column];
      const float sdr_nits = Luminance(game_colour) * NITS;
      const int band = (sdr_nits >= 1.f && sdr_nits <= 30.f) ? 0 : ((sdr_nits > 30.f && sdr_nits <= 150.f) ? 1 : -1);
      if (band < 0 || sdr_nits <= 1.01f * column_floor) continue;
      const Float3 game_lab = OkLab(game_colour);
      const float game_chroma = std::hypot(game_lab[1], game_lab[2]);
      if (game_chroma < 0.01f) continue;
      for (UINT j = 0; j + 1 < HEIGHT; ++j) {
        const Float3 below = OkLab(v35[j * WIDTH + column]);
        const Float3 above = OkLab(v35[(j + 1) * WIDTH + column]);
        if (game_lab[0] < below[0] || game_lab[0] > above[0]) continue;
        const float t = (above[0] > below[0]) ? (game_lab[0] - below[0]) / (above[0] - below[0]) : 0.f;
        log_sums[band] += std::log(std::lerp(std::hypot(below[1], below[2]), std::hypot(above[1], above[2]), t) / game_chroma);
        ++comparison.chroma_counts[band];
        break;
      }
    }
  }
  for (int band = 0; band < 2; ++band) {
    comparison.chroma[band] = static_cast<float>(std::exp(log_sums[band] / std::max(comparison.chroma_counts[band], 1)));
  }
  return comparison;
}

// The fit's score: the worst of grey (0.05 - 1 and 1 - 15 nits, the bands with rows) and the chroma (1 - 30 nits).
static float BelowWhite(const Comparison& c) {
  float worst = std::abs(c.chroma[0] - 1.f);
  for (int band = 0; band < 2; ++band) {
    if (c.grey[band].rows == 0) continue;
    worst = std::max({worst, std::abs(c.grey[band].low - 1.f), std::abs(c.grey[band].high - 1.f)});
  }
  return worst;
}

// PsychoV35's highlights: the worst grey |fitted / own - 1| where Musa's own is 100 nits up to 95 % of Peak.
static float Highlights(const std::vector<Float3>& fitted, const std::vector<Float3>& own, float paper_white, float peak) {
  float worst = 0.f;
  for (UINT y = 0; y < HEIGHT; ++y) {
    const float own_nits = Luminance(own[y * WIDTH]) * paper_white;
    if (own_nits < 100.f || own_nits > 0.95f * peak) continue;
    worst = std::max(worst, std::abs((Luminance(fitted[y * WIDTH]) / Luminance(own[y * WIDTH])) - 1.f));
  }
  return worst;
}

static void Report(const char* name, const std::vector<Float3>& sdr, const std::vector<Float3>& v35) {
  bool sdr_rises = true, v35_rises = true;
  for (UINT y = 1; y < HEIGHT; ++y) {
    sdr_rises = sdr_rises && Luminance(sdr[y * WIDTH]) >= Luminance(sdr[(y - 1) * WIDTH]) - 1e-7f;
    v35_rises = v35_rises && Luminance(v35[y * WIDTH]) >= Luminance(v35[(y - 1) * WIDTH]) - 1e-7f;
  }
  const float sdr_top = Luminance(sdr[(HEIGHT - 1) * WIDTH]) * NITS;
  const Comparison c = Compare(sdr, v35);
  std::printf("%s: PsychoV35 over Rendering Mode SDR below white, 1000 / 203 nits (SDR's floor %.2f nits, PsychoV35 there "
              "%.3f; SDR's top %.0f nits)\n"
              "   grey where SDR is 0.05 - 1 nits x%.3f..x%.3f (%d rows), 1 - 15 nits x%.3f..x%.3f (%d), 15 - 100 nits x%.3f..x%.3f (%d)\n"
              "   colour at equal lightness, SDR 1 - 30 nits x%.3f (%d), 30 - 150 nits x%.3f (%d)\n",
              name, c.floor_nits, Luminance(v35[0]) * NITS, sdr_top, c.grey[0].low, c.grey[0].high, c.grey[0].rows, c.grey[1].low,
              c.grey[1].high, c.grey[1].rows, c.grey[2].low, c.grey[2].high, c.grey[2].rows, c.chroma[0], c.chroma_counts[0],
              c.chroma[1], c.chroma_counts[1]);
  CHECK(sdr_rises && v35_rises);
  CHECK(sdr_top > 100.f);
  CHECK(c.grey[1].rows > 0 && c.grey[2].rows > 0 && c.chroma_counts[0] > 0);
}

// One game's fit and checks. `draw`: the game's chain with the add-on's data; `sdr_si`: Rendering Mode SDR; `own`:
// PsychoV35 at Musa's own sliders and the game's Hue Clip; `shipped`: the add-on's defaults of the four sliders.
template <typename Injection, typename DrawFn>
static void FitGame(const char* name, const DrawFn& draw, const Injection& sdr_si, const Injection& own, const Values& shipped) {
  struct Slider {
    const char* name;
    float low, high;
  };
  constexpr std::array<Slider, 4> SLIDERS = {
      {{"Contrast", 0.8f, 2.f}, {"Shadows", 0.3f, 1.5f}, {"Saturation", 0.7f, 1.3f}, {"Highlight Contrast", 0.6f, 1.2f}}};
  const auto sdr = draw(sdr_si);
  const auto own_ramp = draw(own);
  Report(std::format("{} before (Musa's own sliders)", name).c_str(), sdr, own_ramp);
  // One fit with the guard at 1000 / 203 within `tolerance`: the score first, the guard only for a candidate that beats
  // the best.
  const auto fit = [&](float tolerance) {
    Values best = MUSA;
    float best_score = 1e9f;
    bool first_pass = true;
    const auto start = std::chrono::steady_clock::now();
    for (const auto& [step, reach] : {std::pair{0.1f, 6}, std::pair{0.05f, 2}, std::pair{0.01f, 3}}) {
      const Values centre = (first_pass ? Values{1.4f, 0.9f, 1.f, 0.9f} : best);
      first_pass = false;
      std::array<std::vector<float>, 4> grid;
      for (size_t d = 0; d < SLIDERS.size(); ++d) {
        for (int k = -reach; k <= reach; ++k) {
          const float value = std::round((centre[d] + (static_cast<float>(k) * step)) * 100.f) / 100.f;
          if (value >= SLIDERS[d].low - 1e-4f && value <= SLIDERS[d].high + 1e-4f) grid[d].push_back(value);
        }
      }
      for (const float contrast : grid[0]) {
        for (const float shadows : grid[1]) {
          for (const float saturation : grid[2]) {
            for (const float highlight_contrast : grid[3]) {
              const Values values = {contrast, shadows, saturation, highlight_contrast};
              const auto ramp = draw(WithSliders(own, values));
              if (const float score = BelowWhite(Compare(sdr, ramp));
                  score < best_score && Highlights(ramp, own_ramp, NITS, PEAK) <= tolerance) {
                best = values;
                best_score = score;
              }
            }
          }
        }
      }
    }
    std::printf("   fit, highlights within %.0f %% at 1000 / 203: %.2f / %.2f / %.2f / %.2f (Contrast / Shadows / Saturation / "
                "Highlight Contrast), worst %.2f %% (%.0f s)\n",
                100.f * tolerance, best[0], best[1], best[2], best[3], 100.f * best_score,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    return std::pair{best, best_score};
  };
  // The smallest tolerance whose best is under 5 %.
  float tolerance = 0.f;
  float chosen_score = 1e9f;
  for (const float candidate_tolerance : {0.1f, 0.15f, 0.2f}) {
    const auto [best, best_score] = fit(candidate_tolerance);
    if (tolerance == 0.f && best_score < 0.05f) {
      tolerance = candidate_tolerance;
      chosen_score = best_score;
    }
  }
  const float own_score = BelowWhite(Compare(sdr, own_ramp));
  const float control_score = BelowWhite(Compare(sdr, draw(WithSliders(own, {1.2f, 1.f, 1.2f, 1.f}))));
  const auto shipped_si = WithSliders(own, shipped);
  const auto shipped_ramp = draw(shipped_si);
  const float shipped_score = BelowWhite(Compare(sdr, shipped_ramp));
  std::printf("%s V35 defaults %.2f / %.2f / %.2f / %.2f: worst %.2f %% (the %.0f %% guard's fit: %.2f %%); Musa's own %.2f %%; "
              "control Contrast and Saturation 1.2 (must fail 5 %%): %.1f %%\n",
              name, shipped[0], shipped[1], shipped[2], shipped[3], 100.f * shipped_score, 100.f * tolerance, 100.f * chosen_score,
              100.f * own_score, 100.f * control_score);
  CHECK(tolerance != 0.f);
  CHECK(shipped_score < 0.05f);
  CHECK(shipped_score <= chosen_score + 0.005f);
  CHECK(control_score > 0.05f);
  Report(std::format("{} after (the defaults)", name).c_str(), sdr, shipped_ramp);
  auto shipped_2500 = shipped_si;
  auto own_2500 = own;
  for (auto* si : {&shipped_2500, &own_2500}) {
    si->peak_white_nits = 2500.f;
    si->diffuse_white_nits = si->graphics_white_nits = 275.f;
  }
  const float at_1000 = Highlights(shipped_ramp, own_ramp, NITS, PEAK);
  const float at_2500 = Highlights(draw(shipped_2500), draw(own_2500), 275.f, 2500.f);
  std::printf("   highlights against Musa's own (100 nits - 95 %% of Peak): %.1f %% at 1000 / 203, %.1f %% at 2500 / 275\n",
              100.f * at_1000, 100.f * at_2500);
  CHECK(at_1000 <= tolerance + 1e-4f);
  CHECK(at_2500 <= 0.2f);
}

int main(int argc, char** argv) {
  REQUIRE(argc >= 2);
  const std::filesystem::path folder = argv[1];
  const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &device, nullptr, &context)));
  const char* vertex_source =
      "struct O { float4 position : SV_Position; float2 uv : TEXCOORD0; };"
      "O main(uint id : SV_VertexID) { O o; o.uv = float2((id << 1) & 2, id & 2);"
      "o.position = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1); return o; }";
  ComPtr<ID3DBlob> vertex_code;
  REQUIRE(SUCCEEDED(D3DCompile(vertex_source, std::strlen(vertex_source), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &vertex_code, nullptr)));
  REQUIRE(SUCCEEDED(device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, &full_screen)));
  const D3D11_SAMPLER_DESC sampler_desc = {D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP,
                                           D3D11_TEXTURE_ADDRESS_CLAMP, 0, 1, D3D11_COMPARISON_NEVER, {}, 0, D3D11_FLOAT32_MAX};
  REQUIRE(SUCCEEDED(device->CreateSamplerState(&sampler_desc, &linear_clamp)));
  std::map<std::string, Pair> shaders;
  for (const char* name : {"r_t00", "r_t01", "th_t00", "th_t01"}) {
    const Shader shader = LoadPixelShader(folder / (std::string(name) + ".cso"));
    shaders[name] = {shader, shader};
  }
  Chain chain = {};
  chain.rome2_tone_mapper = &shaders["r_t00"];
  chain.rome2_hdr_to_screen = &shaders["r_t01"];
  chain.thrones_tone_mapper = &shaders["th_t00"];
  chain.thrones_hdr_to_screen = &shaders["th_t01"];
  const auto scene = FitScene();

  // Rome II: Recommended's Hue Clip 0.8 (HUE_CLIP_DEFAULT 80), SDR without it (ReportToe's).
  FitGame(
      "Rome II", [&](const rome2::ShaderInjectData& si) { return RunRome2(chain, scene, si); }, Rome2Injection(0.f, 0.f),
      Rome2Injection(3.f, 0.8f), {ROME2_V35_CONTRAST, ROME2_V35_SHADOWS, ROME2_V35_SATURATION, ROME2_V35_HIGHLIGHT_CONTRAST});
  // A scene the fit did not see: Rome II on Attila's S-shaped curve (test/tw_psychov35's AttilaSCurve), before and after.
  const auto s_curve = [&](const rome2::ShaderInjectData& si) { return RunRome2(chain, scene, si, false); };
  const auto s_curve_sdr = s_curve(Rome2Injection(0.f, 0.f));
  Report("Rome II, S-shaped curve, before", s_curve_sdr, s_curve(Rome2Injection(3.f, 0.8f)));
  Report("Rome II, S-shaped curve, after", s_curve_sdr,
         s_curve(WithSliders(Rome2Injection(3.f, 0.8f),
                             {ROME2_V35_CONTRAST, ROME2_V35_SHADOWS, ROME2_V35_SATURATION, ROME2_V35_HIGHLIGHT_CONTRAST})));
  // Thrones of Britannia: Hue Clip 0.4 (HUE_CLIP_DEFAULT 40, ThronesInjection's); its defaults Musa's own (the fit's best).
  FitGame(
      "Thrones of Britannia", [&](const thrones::ShaderInjectData& si) { return RunThrones(chain, scene, si); },
      ThronesInjection(0.f, 1.f), ThronesInjection(3.f, 1.f), MUSA);
  std::printf(failures == 0 ? "PASS\n" : "FAILED (%d)\n", failures);
  return failures == 0 ? 0 : 1;
}
