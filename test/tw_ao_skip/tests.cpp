// twattila/ao_skip.hpp: SkipGameSsao, the one rule for leaving the game's SSAO chain out under Ray-Traced AO (Warhammer,
// Warhammer II, Warhammer III, Troy), over rt_scene's runtime as its Update / Trace leave it.
// 1. Ray-Traced AO on, the bridge tracing, traced this frame or the last: skipped (frames 0, 1, 5 and 2^40).
// 2. CONTROLS (the game's SSAO stays): traced two frames ago or earlier; Ray-Traced AO off; Path-Traced Ambient alone; the
//    bridge not tracing; nothing traced yet (traced_frame UINT64_MAX) at frame 0 and later.
// 3. The CONTROL that must differ: the rule the Warhammer / Warhammer III / Troy copies had (traced_frame == frame ||
//    traced_frame + 1 == frame) skips at frame 0 with nothing traced (UINT64_MAX + 1 wraps to 0); the shared rule doesn't.
// No GPU work. tests.exe
#include <windows.h>

#include <cstdint>
#include <cstdio>

#include "src/games/twattila/ao_skip.hpp"

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

extern "C" __declspec(dllexport) const char* const NAME = "AO skip test";
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}

int main() {
  auto& runtime = twattila::rt_scene::internal::runtime;
  auto& bridge = twattila::dlaa_bridge::bridge;
  const auto set = [&](bool ambient_occlusion, bool path_ambient, bool ray_tracing, uint64_t traced_frame, uint64_t frame) {
    const std::unique_lock lock(runtime.mutex);
    runtime.settings.ambient_occlusion = ambient_occlusion;
    runtime.settings.path_ambient = path_ambient;
    runtime.traced_frame = traced_frame;
    runtime.frame = frame;
    bridge.ray_tracing = ray_tracing;
  };
  const auto skip = twattila::ao_skip::SkipGameSsao;
  const auto old_rule = [&] {
    const std::unique_lock lock(runtime.mutex);
    return runtime.settings.ambient_occlusion && bridge.ray_tracing
           && (runtime.traced_frame == runtime.frame || runtime.traced_frame + 1 == runtime.frame);
  };

  // 1.
  for (const uint64_t frame : {uint64_t{0}, uint64_t{1}, uint64_t{5}, uint64_t{1} << 40}) {
    set(true, false, true, frame, frame);
    CHECK(skip());
    if (frame > 0) {
      set(true, false, true, frame - 1, frame);
      CHECK(skip());
    }
  }
  std::printf("1 traced this frame or the last: skipped: ok\n");

  // 2.
  set(true, false, true, 3, 5);
  CHECK(!skip());
  set(false, false, true, 5, 5);
  CHECK(!skip());
  set(false, true, true, 5, 5);
  CHECK(!skip());
  set(true, false, false, 5, 5);
  CHECK(!skip());
  for (const uint64_t frame : {uint64_t{0}, uint64_t{1}, uint64_t{7}}) {
    set(true, false, true, UINT64_MAX, frame);
    CHECK(!skip());
  }
  std::printf("2 CONTROLS (older trace, rays' AO off, paths alone, bridge off, nothing traced): the game's: ok\n");

  // 3.
  set(true, false, true, UINT64_MAX, 0);
  CHECK(old_rule() && !skip());
  std::printf("3 CONTROL: the old copies' rule skips at frame 0 with nothing traced, the shared one doesn't: ok\n");
  std::printf("AO SKIP ALL PASSED\n");
  return 0;
}
