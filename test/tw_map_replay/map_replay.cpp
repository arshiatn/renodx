// tw_map_replay: P0.d's replay of a map trace (map_replay.hpp) from the command line (run.ps1 builds it).
//   map_replay <tw_map_trace_v3.txt | tw_map_trace.txt | folder> [options]
//   --mode baseline|parity|overhead (baseline)
//     baseline: --runs fresh devices (2), each with the wait locator's table; the mark: CB DISCARD time >= --mark-ms
//       (5) a frame in every run.
//     parity: native, then --plugin with --control, each draw's strip compared bit for bit (exit 1 on a difference).
//     overhead: --runs per mode (20), native and --plugin alternating ABBA; the render thread's D3D11 call time a frame
//       (Present left out) and of the calls the ring hooks per mode, their 95 % intervals and the differences.
//   --nvidia (else WARP)  --frames a-b  --loops n (1)  --pace on|off (on)  --gpu-ms x (0)  --target n (2048)
//   --plugin native|ring  --control none|upload|window|offset  --wait-ms x (0.1)  --warm n (1: frames left out of the
//   summary)  --waits <join file> (v2: the ETL waits left out of the pacing)  --gap-cap-ms x (0.5, v2 without waits)
//   --addon-buffers native|ring (native: the plugin gets the game's buffers alone, those mapped (tw_map_trace.txt) or
//   made (v3 buffer lines) through ReShade's proxy; ring: ReShade's own too, as before H0)
// Exit 2 with "STOP:" on an error (a removed device, a failed call): GPU runs end there.
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <string>
#include <vector>

#include "./map_replay.hpp"

namespace {

using tw_map_replay::Where;

struct Spread {
  double mean = 0.;
  double half_interval = 0.;  // 95 %
  double variance_of_mean = 0.;
};

Spread SpreadOf(const std::vector<double>& values) {
  Spread spread;
  if (values.empty()) return spread;
  spread.mean = std::accumulate(values.begin(), values.end(), 0.) / static_cast<double>(values.size());
  double squares = 0.;
  for (const double value : values) squares += (value - spread.mean) * (value - spread.mean);
  const double variance = (values.size() > 1 ? squares / static_cast<double>(values.size() - 1) : 0.);
  spread.variance_of_mean = variance / static_cast<double>(values.size());
  spread.half_interval = 1.96 * std::sqrt(spread.variance_of_mean);
  return spread;
}

}  // namespace

int main(int argc, char** argv) {
  std::string mode = "baseline";
  std::filesystem::path trace;
  std::filesystem::path waits;
  double gap_cap_ms = 0.5;
  uint32_t runs = 0;
  uint32_t warm = 1;
  double mark_ms = 5.;
  std::string plugin_name = "native";
  tw_map_replay::Options options;
  try {
    for (int index = 1; index < argc; ++index) {
      const std::string_view argument = argv[index];
      const auto value = [&]() -> std::string_view {
        if (index + 1 >= argc) throw std::runtime_error(std::format("{} needs a value", argument));
        return argv[++index];
      };
      if (argument == "--mode") {
        mode = value();
      } else if (argument == "--nvidia") {
        options.warp = false;
      } else if (argument == "--frames") {
        const std::string_view frames = value();
        const size_t dash = frames.find('-');
        options.first_frame = static_cast<uint32_t>(tw_map_replay::internal::Number(frames.substr(0, dash)));
        options.last_frame = (dash == std::string_view::npos
                                  ? options.first_frame
                                  : static_cast<uint32_t>(tw_map_replay::internal::Number(frames.substr(dash + 1))));
      } else if (argument == "--loops") {
        options.loops = static_cast<uint32_t>(tw_map_replay::internal::Number(value()));
      } else if (argument == "--runs") {
        runs = static_cast<uint32_t>(tw_map_replay::internal::Number(value()));
      } else if (argument == "--pace") {
        options.pace = (value() == "on");
      } else if (argument == "--gpu-ms") {
        options.gpu_ms = tw_map_replay::internal::Decimal(value());
      } else if (argument == "--target") {
        options.target_size = static_cast<uint32_t>(tw_map_replay::internal::Number(value()));
      } else if (argument == "--plugin") {
        plugin_name = value();
      } else if (argument == "--control") {
        const std::string_view control = value();
        constexpr std::array<std::string_view, 4> CONTROLS = {"none", "upload", "window", "offset"};
        const auto found = std::ranges::find(CONTROLS, control);
        if (found == CONTROLS.end()) throw std::runtime_error(std::format("unknown control {}", control));
        options.control = static_cast<tw_map_replay::Control>(found - CONTROLS.begin());
      } else if (argument == "--addon-buffers") {
        const std::string_view offer = value();
        if (offer != "native" && offer != "ring") throw std::runtime_error(std::format("unknown --addon-buffers {}", offer));
        options.addon_buffers = (offer == "ring");
      } else if (argument == "--wait-ms") {
        options.wait_ms = tw_map_replay::internal::Decimal(value());
      } else if (argument == "--warm") {
        warm = static_cast<uint32_t>(tw_map_replay::internal::Number(value()));
      } else if (argument == "--mark-ms") {
        mark_ms = tw_map_replay::internal::Decimal(value());
      } else if (argument == "--waits") {
        waits = value();
      } else if (argument == "--gap-cap-ms") {
        gap_cap_ms = tw_map_replay::internal::Decimal(value());
      } else if (!argument.starts_with("--") && trace.empty()) {
        trace = argument;
      } else {
        throw std::runtime_error(std::format("unknown argument {}", argument));
      }
    }
    if (trace.empty()) throw std::runtime_error("no trace given");
    const tw_map_replay::RingPlugin* plugin = &tw_map_replay::NATIVE_PLUGIN;
    if (plugin_name == "ring") {
#ifdef TW_MAP_REPLAY_RING_PLUGIN
      plugin = &tw_map_replay::RING_PLUGIN;
#else
      throw std::runtime_error("no ring compiled in (run.ps1 -RingAdapter)");
#endif
    } else if (plugin_name != "native") {
      throw std::runtime_error(std::format("unknown plugin {}", plugin_name));
    }
    const auto stream = tw_map_replay::Load(trace, waits, gap_cap_ms);
    std::printf("stream: v%u, %u frames, %zu calls, %zu resources, %u device context states\n", stream.version,
                stream.frames, stream.calls.size(), stream.resources.size(), stream.states);
    for (const auto& note : stream.notes) std::printf("note: %s\n", note.c_str());
    std::fflush(stdout);

    if (mode == "baseline") {
      options.plugin = plugin;
      std::vector<double> cb_discard;
      for (uint32_t run = 0; run < std::max(runs, 1u); ++run) {
        const auto result = tw_map_replay::Replay(stream, options);
        const auto summary = tw_map_replay::Summarize(result, warm);
        std::printf("== run %u (fresh device), plugin %s\n%s", run + 1, plugin->name,
                    tw_map_replay::Report(stream, result, summary, options.wait_ms).c_str());
        std::string per_frame;
        for (size_t frame = 0; frame < result.frames.size(); ++frame) {
          per_frame += std::format("{}{:.2f}", (per_frame.empty() ? "" : " "),
                                   static_cast<double>(result.frames[frame].ticks[static_cast<size_t>(Where::CB_DISCARD)])
                                       * 1000. / static_cast<double>(result.frequency));
        }
        std::printf("cb_discard ms per frame: %s\n", per_frame.c_str());
        std::fflush(stdout);
        cb_discard.push_back(summary.ms[static_cast<size_t>(Where::CB_DISCARD)]);
      }
      std::string runs_text;
      bool reproduced = true;
      for (const double value : cb_discard) {
        runs_text += std::format("{}{:.2f}", (runs_text.empty() ? "" : ", "), value);
        reproduced = reproduced && value >= mark_ms;
      }
      std::printf("MARK cb_discard >= %.2f ms/f in every run: %s (runs: %s ms/f)\n", mark_ms,
                  (reproduced ? "REPRODUCED" : "NOT reproduced"), runs_text.c_str());
      return 0;
    }
    if (mode == "parity") {
      options.parity = true;
      options.pace = false;
      options.plugin = &tw_map_replay::NATIVE_PLUGIN;
      const tw_map_replay::Control control = options.control;
      options.control = tw_map_replay::Control::NONE;
      const auto off = tw_map_replay::Replay(stream, options);
      options.plugin = plugin;
      options.control = control;
      const auto on = tw_map_replay::Replay(stream, options);
      const std::string difference = tw_map_replay::CompareStrips(off, on);
      size_t draws = 0;
      for (const uint32_t frame_draws : off.draws_per_frame) draws += frame_draws;
      std::printf("parity native vs %s (control %d): %zu frames, %zu draws: %s\n", plugin->name,
                  static_cast<int>(control), off.strips.size(), draws,
                  (difference.empty() ? "EQUAL bit for bit" : difference.c_str()));
      return (difference.empty() ? 0 : 1);
    }
    if (mode == "overhead") {
      const uint32_t per_mode = std::max(runs, 20u);
      // [metric][side]: every D3D11 call but Present; the hooked calls only.
      std::array<std::array<std::vector<double>, 2>, 2> samples;
      for (uint32_t pair = 0; pair < per_mode; ++pair) {
        for (uint32_t step = 0; step < 2; ++step) {
          const uint32_t side = ((pair % 2 == 0) ? step : 1 - step);  // ABBA
          options.plugin = (side == 0 ? &tw_map_replay::NATIVE_PLUGIN : plugin);
          const auto summary = tw_map_replay::Summarize(tw_map_replay::Replay(stream, options), warm);
          samples[0][side].push_back(summary.call_ms);
          samples[1][side].push_back(summary.hooked_ms);
        }
      }
      std::printf("overhead: %u runs a mode (ABBA, fresh device each), %s against native\n", per_mode, plugin->name);
      constexpr std::array<const char*, 2> METRICS = {"D3D11 calls (not Present)", "hooked calls"};
      for (size_t metric = 0; metric < METRICS.size(); ++metric) {
        const auto native = SpreadOf(samples[metric][0]);
        const auto plugged = SpreadOf(samples[metric][1]);
        std::printf("  %-26s native %.4f +- %.4f, %s %.4f +- %.4f, added %+.4f +- %.4f ms/f (95 %%)\n",
                    METRICS[metric], native.mean, native.half_interval, plugin->name, plugged.mean,
                    plugged.half_interval, plugged.mean - native.mean,
                    1.96 * std::sqrt(native.variance_of_mean + plugged.variance_of_mean));
      }
      return 0;
    }
    throw std::runtime_error(std::format("unknown mode {}", mode));
  } catch (const std::exception& error) {
    std::printf("STOP: %s\n", error.what());
    return 2;
  }
}
