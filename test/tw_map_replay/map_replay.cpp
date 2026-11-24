// tw_map_replay: P0.d's replay of a map trace (map_replay.hpp) from the command line (run.ps1 builds it).
//   map_replay <tw_map_trace_v3.txt | tw_map_trace.txt | folder> [options]
//   --mode baseline|parity|overhead (baseline)
//     baseline: --runs fresh devices (2), each with the wait locator's table; the mark: CB DISCARD time >= --mark-ms
//       (5) a frame in every run.
//     parity: native, then --plugin with --control, each draw's strip compared bit for bit (exit 1 on a difference).
//     overhead: --runs per mode (20), native and --plugin alternating ABBA; the render thread's D3D11 call time a frame
//       (Present left out) and of the calls the ring hooks per mode, their 95 % intervals and the differences.
//   --nvidia (else WARP)  --frames a-b  --loops n (1)  --pace on|off (on)  --gpu-ms x (0)  --target n (2048)
//   --plugin native|ring  --control none|upload|window|offset|park  --wait-ms x (0.1)  --warm n (1: frames left out of
//   the summary)  --waits <join file> (v2: the ETL waits left out of the pacing)  --gap-cap-ms x (0.5, v2 without waits)
//   --addon-buffers native|ring (native: the plugin gets the game's buffers alone, those mapped (tw_map_trace.txt) or
//   made (v3 buffer lines) through ReShade's proxy; ring: ReShade's own too, as before H0)
//   The trap test (s5r_ design v3 3.3; seconds from the first replayed frame's start): --park cb|vb|all,<start>,<length>
//   (the victims' maps and binds to stand-ins in the window: cb the game's dynamic CBs <= 64 KB, vb its dynamic VBs
//   > 64 KB, all every dynamic buffer of its; parity: the ON run only)  --keep-warm n (a native DISCARD of each victim
//   every n parked frames)  --keep-warm-bind (and one draw with it bound; not in parity mode)  --plugin-window
//   <start>,<length> (the plugin's row on only in it). Baseline mode then prints the phases around each window given
//   (the park's, the focus gap's, the plugin's; none: around the largest step a scan finds) and a TRAP line per DISCARD
//   row (yes: after and late both >= max(2, 5x before), or both >= before + 2 ms/f).
//   Trigger attempts (PK1 6.2 H5): --piece-cap n (the plugin's most pieces)  --burst ms (each frame's calls unpaced,
//   then idle to ms from its start)  --extra push,<n> (n DISCARDs of a 432 B constant buffer a frame, between draws)
//   --extra units,<n> (n 16 B UpdateSubresource + Dispatch a frame)  --ballast-mb n (n MB of address space reserved,
//   nothing committed)  --focus-gap <start>,<length> (the window full-screen in the foreground, a second window of the
//   harness takes the focus in the gap: it takes the desktop's foreground).
// Exit 2 with "STOP:" on an error (a removed device, a failed call): GPU runs end there.
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <string>
#include <tuple>
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
  uint32_t ballast_mb = 0;
  std::string plugin_name = "native";
  tw_map_replay::Options options;
  try {
    for (int index = 1; index < argc; ++index) {
      const std::string_view argument = argv[index];
      const auto value = [&]() -> std::string_view {
        if (index + 1 >= argc) throw std::runtime_error(std::format("{} needs a value", argument));
        return argv[++index];
      };
      // "<start s>,<length s>"
      const auto window = [](std::string_view text) {
        const size_t comma = text.find(',');
        if (comma == std::string_view::npos) throw std::runtime_error(std::format("not <start>,<length>: '{}'", text));
        return std::pair(tw_map_replay::internal::Decimal(text.substr(0, comma)),
                         tw_map_replay::internal::Decimal(text.substr(comma + 1)));
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
        constexpr std::array<std::string_view, 5> CONTROLS = {"none", "upload", "window", "offset", "park"};
        const auto found = std::ranges::find(CONTROLS, control);
        if (found == CONTROLS.end()) throw std::runtime_error(std::format("unknown control {}", control));
        options.control = static_cast<tw_map_replay::Control>(found - CONTROLS.begin());
      } else if (argument == "--addon-buffers") {
        const std::string_view offer = value();
        if (offer != "native" && offer != "ring") throw std::runtime_error(std::format("unknown --addon-buffers {}", offer));
        options.addon_buffers = (offer == "ring");
      } else if (argument == "--park") {
        const std::string_view park = value();
        const size_t comma = park.find(',');
        constexpr std::array<std::string_view, 4> PARKS = {"none", "cb", "vb", "all"};
        const auto found = std::ranges::find(PARKS, park.substr(0, comma));
        if (found == PARKS.end() || comma == std::string_view::npos) {
          throw std::runtime_error(std::format("not cb|vb|all,<start>,<length>: '{}'", park));
        }
        options.park = static_cast<tw_map_replay::Park>(found - PARKS.begin());
        std::tie(options.park_start_s, options.park_length_s) = window(park.substr(comma + 1));
      } else if (argument == "--keep-warm") {
        options.keep_warm = static_cast<uint32_t>(tw_map_replay::internal::Number(value()));
      } else if (argument == "--keep-warm-bind") {
        options.keep_warm_bind = true;
      } else if (argument == "--plugin-window") {
        std::tie(options.plugin_start_s, options.plugin_length_s) = window(value());
      } else if (argument == "--piece-cap") {
        options.piece_cap = static_cast<uint32_t>(tw_map_replay::internal::Number(value()));
      } else if (argument == "--burst") {
        options.burst_ms = tw_map_replay::internal::Decimal(value());
      } else if (argument == "--extra") {
        const std::string_view extra = value();
        const size_t comma = extra.find(',');
        const std::string_view kind = extra.substr(0, comma);
        if (comma == std::string_view::npos || (kind != "push" && kind != "units")) {
          throw std::runtime_error(std::format("not push|units,<n>: '{}'", extra));
        }
        (kind == "push" ? options.extra_push : options.extra_units) =
            static_cast<uint32_t>(tw_map_replay::internal::Number(extra.substr(comma + 1)));
      } else if (argument == "--ballast-mb") {
        ballast_mb = static_cast<uint32_t>(tw_map_replay::internal::Number(value()));
      } else if (argument == "--focus-gap") {
        std::tie(options.focus_start_s, options.focus_length_s) = window(value());
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
    if (ballast_mb != 0) {
      const uint32_t reserved = tw_map_replay::ReserveBallast(ballast_mb);
      MEMORYSTATUSEX memory = {.dwLength = sizeof(memory)};
      GlobalMemoryStatusEx(&memory);
      std::printf("ballast: %u of %u MB of address space reserved, nothing committed; %llu of %llu MB left\n", reserved,
                  ballast_mb, memory.ullAvailVirtual >> 20, memory.ullTotalVirtual >> 20);
      if (reserved < ballast_mb) throw std::runtime_error("ballast: the address space ran out");
    }
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
        if (options.focus_start_s >= 0.) {
          std::printf("focus gap: the second window in the foreground %s, ours again after it %s\n",
                      (result.focus_taken ? "yes" : "no"), (result.focus_back ? "yes" : "no"));
        }
        const bool park = (options.park != tw_map_replay::Park::NONE);
        if (park || options.plugin_start_s >= 0.) {
          std::printf("park: %u victims, %u maps to their stand-ins, %u keep-warm DISCARDs\n", result.victims,
                      result.parked_maps, result.warm_maps);
        }
        // The phases around each window given (the park's, the focus gap's, the plugin's); none: the scan.
        bool windowed = false;
        for (const auto& [start_s, length_s] : {std::pair((park ? options.park_start_s : -1.), options.park_length_s),
                                                std::pair(options.focus_start_s, options.focus_length_s),
                                                std::pair(options.plugin_start_s, options.plugin_length_s)}) {
          if (start_s < 0.) continue;
          windowed = true;
          std::printf("%s", tw_map_replay::PhaseReport(result, start_s, length_s).c_str());
        }
        if (!windowed) std::printf("%s", tw_map_replay::ScanReport(result).c_str());
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
      const tw_map_replay::Park park = options.park;
      options.control = tw_map_replay::Control::NONE;
      options.park = tw_map_replay::Park::NONE;
      const auto off = tw_map_replay::Replay(stream, options);
      options.plugin = plugin;
      options.control = control;
      options.park = park;
      const auto on = tw_map_replay::Replay(stream, options);
      if (park != tw_map_replay::Park::NONE) {
        std::printf("park: %u victims, %u maps to their stand-ins\n", on.victims, on.parked_maps);
      }
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
