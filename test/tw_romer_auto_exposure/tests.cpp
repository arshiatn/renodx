// Rome Remastered's auto exposure (Feral's 6A1D2178, one thread over the 64-bin histogram of log luminance) against the
// add-on's replacement (src/games/twromeremastered/shaders/autoexposure00_0x6A1D2178.cs_5_0.hlsl) on WARP.
// 1. Off (Modded Auto Exposure 0, every Rendering Mode) and SDR with it on: the add-on's u2 equals the game's to the bit
//    on random histograms (summing to the pixels the game counts), three constant sets and a first frame (u2.z 0) as well
//    as a running one. CONTROL: a tenth of the middle bin's pixels a bin up changes the game's output.
// 2. The walk as read from the assembly (a C++ model in double) gives the game's x (exposure) and z (bright level) within
//    1e-5: the reading the replacement follows. CONTROL: the model without the low share's partial bin is off.
// 3. On (HDR, Sensitivity 0.6 / 2 / 4): x is the model's mean with every bin's log capped at the mean + Sensitivity
//    stops (4 steps from the stock mean) within 1e-5; y, z, w the game's to the bit (the bright level stays the game's).
//    CONTROL: on differs from the game's where bins in the window lie above the cap.
// 4. Report: a battle-like histogram (ground, sky, a fire tail): the exposure on against off, in stops.
// tests.exe <the game's 0x6A1D2178.cs_5_0.cso> <the add-on's build cso (build\twromeremastered.include\embed)>
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <vector>

#include "src/games/twromeremastered/shared.h"
#undef SI
#undef HDR
#undef HDR_PEAK
#undef HDR_INTSCALING
#undef HDR_STOPS

using Microsoft::WRL::ComPtr;

#define REQUIRE(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(2); } } while (false)

constexpr uint32_t BINS = 64;
constexpr uint32_t WIDTH = 3840;
constexpr uint32_t HEIGHT = 2160;

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;

static std::vector<uint8_t> ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

struct Constants {
  std::array<float, 12> cb0;  // [0].xy the counted size (uints), [1] max / min log, low / high share, [2] dt, up, down, k
};

static Constants MakeConstants(float max_log, float min_log, float low, float high, float dt, float up, float down, float k) {
  return {.cb0 = {std::bit_cast<float>(WIDTH), std::bit_cast<float>(HEIGHT), 0.f, 0.f, max_log, min_log, low, high, dt, up, down, k}};
}

// One dispatch of `shader` over `histogram` with last frame's state `previous`: u2[0].
static std::array<float, 4> Run(ID3D11ComputeShader* shader, const Constants& constants, const std::array<uint32_t, BINS>& histogram,
                                const std::array<float, 4>& previous, const ShaderInjectData& injection) {
  const D3D11_BUFFER_DESC cb_desc = {.ByteWidth = 48, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_SUBRESOURCE_DATA cb_data = {.pSysMem = constants.cb0.data()};
  ComPtr<ID3D11Buffer> cb;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&cb_desc, &cb_data, cb.GetAddressOf())));
  const UINT injection_size = (sizeof(ShaderInjectData) + 15u) & ~15u;
  std::vector<uint8_t> injection_bytes(injection_size, 0);
  std::memcpy(injection_bytes.data(), &injection, sizeof(injection));
  const D3D11_BUFFER_DESC si_desc = {.ByteWidth = injection_size, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_SUBRESOURCE_DATA si_data = {.pSysMem = injection_bytes.data()};
  ComPtr<ID3D11Buffer> si;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&si_desc, &si_data, si.GetAddressOf())));
  const D3D11_BUFFER_DESC hist_desc = {.ByteWidth = BINS * 4, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE,
                                       .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, .StructureByteStride = 4};
  const D3D11_SUBRESOURCE_DATA hist_data = {.pSysMem = histogram.data()};
  ComPtr<ID3D11Buffer> hist;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&hist_desc, &hist_data, hist.GetAddressOf())));
  ComPtr<ID3D11ShaderResourceView> hist_view;
  REQUIRE(SUCCEEDED(device->CreateShaderResourceView(hist.Get(), nullptr, hist_view.GetAddressOf())));
  const D3D11_BUFFER_DESC state_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_UNORDERED_ACCESS,
                                        .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, .StructureByteStride = 16};
  const D3D11_SUBRESOURCE_DATA state_data = {.pSysMem = previous.data()};
  ComPtr<ID3D11Buffer> state;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&state_desc, &state_data, state.GetAddressOf())));
  ComPtr<ID3D11UnorderedAccessView> state_view;
  REQUIRE(SUCCEEDED(device->CreateUnorderedAccessView(state.Get(), nullptr, state_view.GetAddressOf())));
  const D3D11_BUFFER_DESC read_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
  ComPtr<ID3D11Buffer> read;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&read_desc, nullptr, read.GetAddressOf())));

  context->CSSetShader(shader, nullptr, 0);
  context->CSSetConstantBuffers(0, 1, cb.GetAddressOf());
  context->CSSetConstantBuffers(13, 1, si.GetAddressOf());
  context->CSSetShaderResources(2, 1, hist_view.GetAddressOf());
  context->CSSetUnorderedAccessViews(2, 1, state_view.GetAddressOf(), nullptr);
  context->Dispatch(1, 1, 1);
  ID3D11UnorderedAccessView* no_view = nullptr;
  context->CSSetUnorderedAccessViews(2, 1, &no_view, nullptr);
  context->CopyResource(read.Get(), state.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  REQUIRE(SUCCEEDED(context->Map(read.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
  std::array<float, 4> result = {};
  std::memcpy(result.data(), mapped.pData, 16);
  context->Unmap(read.Get(), 0);
  return result;
}

// The walk as read from 6A1D2178's assembly, in double: the window's mean log (each value at most `cap`) and the bright
// level. `partial_low` false: the CONTROL's misreading (the low share's bin taken whole).
struct Walk {
  double mean = 0.0;
  double bright = 0.0;
};
static Walk Model(const Constants& constants, const std::array<uint32_t, BINS>& histogram, double cap, bool partial_low = true) {
  const double pixels = static_cast<double>(WIDTH) * HEIGHT;
  const double max_log = constants.cb0[4];
  const double min_log = constants.cb0[5];
  const double low = pixels * constants.cb0[6];
  const double high = pixels * constants.cb0[7];
  const auto bin_log = [&](uint32_t bin) { return (bin + 0.5) * (max_log - min_log) / 63.0 + min_log; };
  double weight = 0.0;
  double sum = 0.0;
  bool in_window = false;
  uint64_t before = 0;
  Walk walk;
  for (uint32_t bin = 0; bin < BINS; ++bin) {
    const double count = histogram[bin];
    const uint64_t through = before + histogram[bin];
    const double value = std::min(bin_log(bin), cap);
    if (static_cast<double>(through) >= high) {
      const double part = high - static_cast<double>(before);
      const double share = part / count;
      sum += part * value;
      weight += part;
      const double luminance = std::exp(bin_log(bin));
      const double next = (bin + 1 < BINS) ? histogram[bin + 1] : 0.0;
      if (bin != BINS - 1 && next > 0.0) {
        const double next_luminance = std::exp(bin_log(bin + 1));
        walk.bright = (count * luminance * (1.0 - share) + share * next * next_luminance) / (count * (1.0 - share) + share * next);
      } else {
        walk.bright = luminance;
      }
      break;
    }
    if (in_window) {
      sum += count * value;
      weight += count;
    } else if (static_cast<double>(through) >= low) {
      const double above = partial_low ? (1.0 - (low - static_cast<double>(before)) / count) : 1.0;
      sum += count * above * value;
      weight += above * count;
      in_window = true;
    }
    before = through;
  }
  walk.mean = sum / weight;
  return walk;
}

static double Exposure(double mean) {
  const double average = std::exp(mean);
  return (1.03 - 2.0 / (std::log10(average + 1.0) + 2.0)) / average;
}

// A random histogram of WIDTH x HEIGHT pixels: a few lumps of log luminance over the bins.
static std::array<uint32_t, BINS> RandomHistogram(std::mt19937* random) {
  std::array<double, BINS> shape = {};
  std::uniform_real_distribution<double> uniform(0.0, 1.0);
  const int lumps = 1 + static_cast<int>(uniform(*random) * 4.0);
  for (int lump = 0; lump < lumps; ++lump) {
    const double centre = uniform(*random) * 63.0;
    const double width = 0.7 + uniform(*random) * 8.0;
    const double mass = 0.1 + uniform(*random);
    for (uint32_t bin = 0; bin < BINS; ++bin) shape[bin] += mass * std::exp(-0.5 * std::pow((bin - centre) / width, 2.0));
  }
  if (uniform(*random) < 0.5) {
    for (uint32_t bin = 0; bin < BINS; ++bin) {
      if (uniform(*random) < 0.2) shape[bin] = 0.0;  // empty bins
    }
  }
  double total = 0.0;
  for (const double value : shape) total += value;
  if (total <= 0.0) shape[31] = total = 1.0;
  std::array<uint32_t, BINS> histogram = {};
  uint64_t given = 0;
  for (uint32_t bin = 0; bin < BINS; ++bin) {
    histogram[bin] = static_cast<uint32_t>(shape[bin] / total * WIDTH * HEIGHT);
    given += histogram[bin];
  }
  histogram[static_cast<uint32_t>(uniform(*random) * 63.0)] += static_cast<uint32_t>(static_cast<uint64_t>(WIDTH) * HEIGHT - given);
  return histogram;
}

static ShaderInjectData Injection(float hdr, float protection, float headroom) {
  ShaderInjectData injection = {};
  injection.hdr = hdr;
  injection.peak_white_nits = 1000.f;
  injection.diffuse_white_nits = 203.f;
  injection.graphics_white_nits = 203.f;
  injection.auto_exposure_highlight_protection = protection;
  injection.auto_exposure_highlight_headroom_stops = headroom;
  return injection;
}

static bool Same(const std::array<float, 4>& a, const std::array<float, 4>& b) {
  return std::memcmp(a.data(), b.data(), sizeof(a)) == 0;
}

int main(int argc, char** argv) {
  REQUIRE(argc == 3);
  const auto game_bytes = ReadFile(argv[1]);
  const auto addon_bytes = ReadFile(argv[2]);
  REQUIRE(!game_bytes.empty() && !addon_bytes.empty());
  REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(),
                                      nullptr, context.GetAddressOf())));
  ComPtr<ID3D11ComputeShader> game;
  ComPtr<ID3D11ComputeShader> addon;
  REQUIRE(SUCCEEDED(device->CreateComputeShader(game_bytes.data(), game_bytes.size(), nullptr, game.GetAddressOf())));
  REQUIRE(SUCCEEDED(device->CreateComputeShader(addon_bytes.data(), addon_bytes.size(), nullptr, addon.GetAddressOf())));

  const std::array<Constants, 3> constant_sets = {
      MakeConstants(2.7725887f, -6.9314718f, 0.4f, 0.95f, 0.0166667f, 3.f, 1.5f, 11.f),
      MakeConstants(4.f, -8.f, 0.7f, 0.99f, 0.0333333f, 1.f, 1.f, 11.f),
      MakeConstants(1.f, -10.f, 0.1f, 0.9f, 0.0083333f, 6.f, 0.5f, 5.f),
  };
  std::mt19937 random(20261116);
  std::uniform_real_distribution<float> level(0.2f, 9.f);

  // 1 and 2.
  int runs = 0;
  double worst_x = 0.0;
  double worst_z = 0.0;
  int control_moved = 0;
  int control_model = 0;
  for (int round = 0; round < 200; ++round) {
    const auto& constants = constant_sets[round % constant_sets.size()];
    const auto histogram = RandomHistogram(&random);
    const std::array<float, 4> previous = {0.f, 0.f, (round % 4 == 0) ? 0.f : level(random), 0.f};
    const auto stock = Run(game.Get(), constants, histogram, previous, Injection(0.f, 0.f, 2.f));
    for (const float hdr : {0.f, 1.f, 2.f, 3.f, 4.f}) {
      const auto off = Run(addon.Get(), constants, histogram, previous, Injection(hdr, 0.f, 2.f));
      if (!Same(off, stock)) {
        std::printf("FAIL off, mode %.0f, round %d: game (%.9g %.9g %.9g %.9g) add-on (%.9g %.9g %.9g %.9g)\n", hdr, round, stock[0],
                    stock[1], stock[2], stock[3], off[0], off[1], off[2], off[3]);
        return 1;
      }
      ++runs;
    }
    const auto sdr_on = Run(addon.Get(), constants, histogram, previous, Injection(0.f, 1.f, 2.f));
    REQUIRE(Same(sdr_on, stock));
    ++runs;
    // CONTROL: a tenth of the pixels of the bin at the window's middle share (between the low and high shares, cb0[1].z /
    // .w: inside the metered window) moved up a bin (down from the last).
    auto moved = histogram;
    const double middle = 0.5 * (constants.cb0[6] + constants.cb0[7]) * WIDTH * HEIGHT;
    uint64_t below = 0;
    for (uint32_t bin = 0; bin < BINS; ++bin) {
      below += moved[bin];
      if (below < middle || moved[bin] == 0) continue;
      const uint32_t count = std::max(moved[bin] / 10u, 1u);
      moved[bin] -= count;
      moved[(bin + 1 < BINS) ? bin + 1 : bin - 1] += count;
      break;
    }
    if (!Same(Run(game.Get(), constants, moved, previous, Injection(0.f, 0.f, 2.f)), stock)) ++control_moved;
    // 2. The model.
    const Walk walk = Model(constants, histogram, INFINITY);
    worst_x = std::max(worst_x, std::abs(Exposure(walk.mean) / stock[0] - 1.0));
    if (previous[2] == 0.f) worst_z = std::max(worst_z, std::abs(walk.bright / stock[2] - 1.0));
    const Walk misread = Model(constants, histogram, INFINITY, false);
    if (std::abs(Exposure(misread.mean) / stock[0] - 1.0) > 1e-5) ++control_model;
  }
  std::printf("1. off in every Rendering Mode and on in SDR: %d runs over 200 histograms, the game's u2 to the bit; CONTROL a tenth of the "
              "middle bin a bin up: the game's output changed in %d of 200\n", runs, control_moved);
  REQUIRE(control_moved >= 190);
  REQUIRE(worst_x < 1e-5 && worst_z < 1e-5);
  std::printf("2. the walk read from the assembly (double): x within %.2g, the first frame's z within %.2g of the game's; CONTROL "
              "the low share's bin whole: x off by more than 1e-5 in %d of 200\n", worst_x, worst_z, control_model);
  REQUIRE(control_model >= 100);

  // 3. On.
  double worst_on = 0.0;
  int differs = 0;
  int capped_cases = 0;
  for (int round = 0; round < 200; ++round) {
    const auto& constants = constant_sets[round % constant_sets.size()];
    const auto histogram = RandomHistogram(&random);
    const std::array<float, 4> previous = {0.f, 0.f, (round % 4 == 0) ? 0.f : level(random), 0.f};
    const float headroom = std::array<float, 3>{0.6f, 2.f, 4.f}[round % 3];
    const auto stock = Run(game.Get(), constants, histogram, previous, Injection(0.f, 0.f, headroom));
    const auto on = Run(addon.Get(), constants, histogram, previous, Injection(4.f, 1.f, headroom));
    REQUIRE(std::memcmp(&on[1], &stock[1], 12) == 0);
    double mean = Model(constants, histogram, INFINITY).mean;
    for (int step = 0; step < 4; ++step) mean = Model(constants, histogram, mean + headroom * std::log(2.0)).mean;
    worst_on = std::max(worst_on, std::abs(Exposure(mean) / on[0] - 1.0));
    const bool capped = std::abs(Exposure(mean) / stock[0] - 1.0) > 1e-4;
    if (capped) {
      ++capped_cases;
      if (on[0] != stock[0]) ++differs;
    }
  }
  std::printf("3. on (HDR; Sensitivity 0.6 / 2 / 4): x within %.2g of the capped model, y / z / w the game's to the bit; CONTROL "
              "on differs from the game's in %d of the %d histograms the cap reaches\n", worst_on, differs, capped_cases);
  REQUIRE(worst_on < 1e-5);
  REQUIRE(capped_cases >= 20 && differs == capped_cases);

  // 4. A battle-like histogram (constant set 0): ground (60%), sky (35%), a fire tail (5%).
  {
    std::array<double, BINS> shape = {};
    const auto& constants = constant_sets[0];
    const auto bin_of = [&](double log_luminance) {
      return (log_luminance - constants.cb0[5]) / (constants.cb0[4] - constants.cb0[5]) * 63.0;
    };
    for (uint32_t bin = 0; bin < BINS; ++bin) {
      shape[bin] = 0.60 * std::exp(-0.5 * std::pow((bin - bin_of(std::log(0.08))) / 4.0, 2.0))
                   + 0.35 * std::exp(-0.5 * std::pow((bin - bin_of(std::log(0.9))) / 1.5, 2.0))
                   + 0.05 * std::exp(-0.5 * std::pow((bin - bin_of(std::log(6.0))) / 2.0, 2.0));
    }
    double total = 0.0;
    for (const double value : shape) total += value;
    std::array<uint32_t, BINS> histogram = {};
    uint64_t given = 0;
    for (uint32_t bin = 0; bin < BINS; ++bin) given += histogram[bin] = static_cast<uint32_t>(shape[bin] / total * WIDTH * HEIGHT);
    histogram[20] += static_cast<uint32_t>(static_cast<uint64_t>(WIDTH) * HEIGHT - given);
    const std::array<float, 4> previous = {0.f, 0.f, 1.f, 0.f};
    const auto stock = Run(game.Get(), constants, histogram, previous, Injection(0.f, 0.f, 2.f));
    std::printf("4. battle-like histogram (ground 0.08, sky 0.9, fire 6; low / high shares 0.4 / 0.95): exposure on against off");
    for (const float headroom : {0.6f, 1.f, 2.f, 3.f, 4.f}) {
      const auto on = Run(addon.Get(), constants, histogram, previous, Injection(4.f, 1.f, headroom));
      std::printf(", %.1f stops %+.3f", headroom, std::log2(on[0] / stock[0]));
    }
    std::printf(" (stops of exposure)\n");
  }
  std::printf("PASS\n");
  return 0;
}
