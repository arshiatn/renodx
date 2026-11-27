// twattila/rt_core.hpp PathFrames: Path-Traced Ambient's run of path-traced Light Bounce frames (TraceLightBounce), every
// host's (Attila, Rome II, Thrones, Troy, WH1 / WH2, Pharaoh, Shogun 2, Ryse, R.U.S.E., Expeditions, Divinity 2). A
// frame sequence as TraceLightBounce sees it: each frame's request counts the run, Path-Traced Ambient is on from
// PATH_AMBIENT_FRAMES of it (as its next line), a traced frame becomes the last bounced one.
// 1. Paths every frame: on at frame 151 (150 counted after the first traced frame), as before.
// 2. The helper misses frame 300, then frames 500-502, then 700-728 (29): Path-Traced Ambient stays on throughout.
//    CONTROL (the rule before, a run only while the frame before was traced): off 150 frames or more after each miss
//    (2.5 s of the game's ambient at 60 fps).
// 3. Paths gone from frame 600 (the helper lost, the game's MSAA): off within AMBIENT_REPLACED_FRAMES + 1 frames, back on
//    150 frames after they return at 800.
// 4. Path Tracing off: no run, off at once.
// 5. The helper made a new scene (RayTracingDevice changed: a lost scene, an idle release): rt_scene::Update starts the run
//    again and turns Path-Traced Ambient off, so the new scene learns its light. An unchanged device keeps both.
//    CONTROL: the rule before kept the run, and the new scene never learned (grey far bounce, no sky).
#include <cstdint>
#include <cstdio>
#include <functional>

#include "src/games/twattila/rt_core.hpp"

extern "C" __declspec(dllexport) const char* const NAME = "Path frames test";
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}

namespace core = twattila::rt_scene::internal;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

struct Run {
  uint64_t first_on = 0;      // the first frame Path-Traced Ambient was on
  uint64_t off_after_on = 0;  // frames it was off again after that
  uint64_t last_on = 0;       // the last frame it was on
};

// Frames 1..`frames` with `count` as the run's rule, `traced(frame)` whether the helper traced that frame's bounce.
static Run Simulate(uint64_t frames, bool path_tracing, const std::function<bool(uint64_t)>& traced,
                    const std::function<uint32_t(uint32_t, bool, uint64_t, uint64_t)>& count) {
  Run run;
  uint32_t path_frames = 0;
  uint64_t bounced_frame = UINT64_MAX;
  for (uint64_t frame = 1; frame <= frames; ++frame) {
    path_frames = count(path_frames, path_tracing, bounced_frame, frame);
    const bool on = path_frames >= core::PATH_AMBIENT_FRAMES;
    if (on && run.first_on == 0) run.first_on = frame;
    if (on) run.last_on = frame;
    if (!on && run.first_on != 0) ++run.off_after_on;
    if (traced(frame)) bounced_frame = frame;
  }
  return run;
}

int main() {
  const auto now = [](uint32_t path_frames, bool path_tracing, uint64_t bounced_frame, uint64_t frame) {
    return core::PathFrames(path_frames, path_tracing, bounced_frame, frame);
  };
  const auto before = [](uint32_t path_frames, bool path_tracing, uint64_t bounced_frame, uint64_t frame) {
    return (path_tracing && bounced_frame + 1 == frame) ? path_frames + 1 : 0u;
  };

  // 1.
  const auto always = [](uint64_t) { return true; };
  const Run steady = Simulate(400, true, always, now);
  CHECK(steady.first_on == core::PATH_AMBIENT_FRAMES + 1 && steady.off_after_on == 0);
  CHECK(Simulate(400, true, always, before).first_on == steady.first_on);
  std::printf("1 paths every frame: Path-Traced Ambient on at frame %llu (the rule before: the same)\n", steady.first_on);

  // 2.
  const auto missed = [](uint64_t frame) { return frame != 300 && (frame < 500 || frame > 502) && (frame < 700 || frame > 728); };
  const Run grace = Simulate(1000, true, missed, now);
  const Run control = Simulate(1000, true, missed, before);
  CHECK(grace.first_on == steady.first_on && grace.off_after_on == 0 && grace.last_on == 1000);
  CHECK(control.off_after_on >= 3 * core::PATH_AMBIENT_FRAMES);
  std::printf("2 missed frames 300, 500-502, 700-728: off 0 frames; CONTROL the rule before: off %llu frames\n", control.off_after_on);

  // 3.
  const auto gone = [](uint64_t frame) { return frame < 600 || frame >= 800; };
  const Run lost = Simulate(1000, true, gone, now);
  const Run lost_short = Simulate(799, true, gone, now);
  CHECK(lost_short.last_on < 600 + core::AMBIENT_REPLACED_FRAMES + 1);
  CHECK(lost.last_on == 1000 && lost.off_after_on == (800 + core::PATH_AMBIENT_FRAMES) - (lost_short.last_on + 1));
  std::printf("3 paths gone at 600: last on at frame %llu, on again at frame %llu (%llu frames off)\n", lost_short.last_on,
              lost_short.last_on + lost.off_after_on + 1, lost.off_after_on);

  // 4.
  const Run off = Simulate(400, false, always, now);
  CHECK(off.first_on == 0);
  std::printf("4 Path Tracing off: never on\n");

  // 5.
  static twattila::dlaa_bridge::Shared shared = {};
  twattila::dlaa_bridge::bridge.shared = &shared;
  twattila::rt_scene::Update({});
  core::runtime.path_frames = 2 * core::PATH_AMBIENT_FRAMES;
  core::runtime.path_ambient_on = true;
  twattila::rt_scene::Update({});
  CHECK(core::runtime.path_frames == 2 * core::PATH_AMBIENT_FRAMES && core::runtime.path_ambient_on);
  ++shared.rt_device;
  twattila::rt_scene::Update({});
  CHECK(core::runtime.path_frames == 0 && !core::runtime.path_ambient_on);
  twattila::dlaa_bridge::bridge.shared = nullptr;
  std::printf("5 a new helper scene: the run starts again, Path-Traced Ambient off\nPASS\n");
  return 0;
}
