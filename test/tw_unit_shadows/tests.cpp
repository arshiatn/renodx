// Attila's soldier shadows under the rays (twattila/graphics_hooks.hpp Scales::unit_shadows, rt_scene.hpp CastersSkipped,
// the addon's Update: `unit_shadows = CastersSkipped() ? 0 : 1`), through the addon's own UnitDistances hook over a stand-in
// for the game's function (its 18 distances at the highest Unit Detail, ATN's v154 game: 16 = 10000).
// 1. Each state of Ray-Traced Shadows Only, as the present after rt_scene::Update finds it: index 16 is 0 only while the sun's
//    shadow map gets no casters next frame (shadows only on, the rays' occlusion made this frame, the map known, the skip
//    installed); else the game's 10000. Every other index the game's in every state.
// 2. The next frame's skip agrees with it (OnBindTargets with the map bound alone: skipped exactly when CastersSkipped was
//    true at the present before; CONTROL: another depth target bound alone is never skipped).
// 3. Back: a frame without the rays' occlusion, or shadows only off, gives 16 the game's value at the next call (the hook
//    writes over the game's output every call; nothing kept).
// 4. CONTROL: Unit Detail 2x on foot soldiers doubles index 0 alone (the hook runs, it is no pass-through).
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include "src/games/twattila/graphics_hooks.hpp"
#include "src/games/twattila/rt_scene.hpp"
#include "test/tw_particle_history/mock_reshade.hpp"

namespace hooks = twattila::graphics_hooks;
namespace scene = twattila::rt_scene;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

extern "C" __declspec(dllexport) const char* const NAME = "Unit shadows test";
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}

// The game's distances at the highest Unit Detail (graphics_hooks.hpp's list).
constexpr std::array<float, 18> GAME = {90.f, 110.f, 200.f, 1000.f, 22.5f, 27.5f, 50.f, 250.f, 40.f,
                                        40.f, 60.f, 500.f, 100.f, 10.f, 10.f, 25.f, 10000.f, FLT_MAX};
constexpr size_t UNIT_SHADOWS = 16;

static float* __fastcall GameUnitDistances(void*, void*, float* out, uint32_t, uint32_t, uint32_t, uint32_t) {
  std::ranges::copy(GAME, out);
  return out;
}

// A device whose views are their resources (the shadow map's depth view is the map).
struct ViewDevice : mock::Device {
  reshade::api::resource get_resource_from_view(reshade::api::resource_view view) const override { return {view.handle}; }
};

int main() {
  hooks::internal::original_unit_distances = &GameUnitDistances;
  hooks::internal::hooks_active = true;
  bool installed = true;  // Update only takes the scales (no module to hook)
  renodx::utils::settings::Setting status;
  // One present of Attila's: the rays' Update first, then the hooks' scales from CastersSkipped, then the game's next call.
  const auto present = [&](bool shadows_only, bool occlusion_this_frame, const std::array<float, 4>& unit_detail) {
    auto& runtime = scene::internal::runtime;
    if (occlusion_this_frame) runtime.occlusion_frame = runtime.frame;
    scene::Settings settings = runtime.settings;
    settings.shadows_only = shadows_only;
    {
      const std::unique_lock lock(runtime.mutex);
      runtime.settings = settings;
      ++runtime.frame;  // rt_core Update's
    }
    hooks::Update({.unit_detail = unit_detail, .unit_shadows = (scene::CastersSkipped() ? 0.f : 1.f)}, &installed, &status);
    std::array<float, 18> out = {};
    hooks::internal::OnUnitDistances(nullptr, nullptr, out.data(), 0, 0, 0, 0);
    return out;
  };
  const auto others_game = [](const std::array<float, 18>& out) {
    for (size_t index = 0; index < out.size(); ++index) {
      if (index != UNIT_SHADOWS && out[index] != GAME[index]) return false;
    }
    return true;
  };
  constexpr std::array<float, 4> GAME_DETAIL = {1.f, 1.f, 1.f, 1.f};

  ViewDevice device;
  mock::CommandList cmd_list;
  cmd_list.owner = &device;
  constexpr uint64_t MAP = 0x1000;
  constexpr uint64_t OTHER_DEPTH = 0x2000;
  const auto next_frame_skips = [&](uint64_t depth) {
    scene::OnBindTargets(&cmd_list, 0, nullptr, {depth});
    const bool skipped = scene::OnCasterDrawIndexed(&cmd_list, 0, 0, 0, 0, 0);
    scene::OnBindTargets(&cmd_list, 1, nullptr, {0});  // the frame's next pass: targets again
    return skipped;
  };

  // The skip not installed (a game without InstallShadowsOnly): the game's, whatever else holds.
  scene::internal::shadow_map = MAP;
  auto out = present(true, true, GAME_DETAIL);
  CHECK(!scene::CastersSkipped() && out[UNIT_SHADOWS] == 10000.f && others_game(out));
  scene::internal::shadows_only_installed = true;  // InstallShadowsOnly's flag (its ReShade events not needed here)

  // Shadows only off: the game's.
  out = present(false, true, GAME_DETAIL);
  CHECK(out[UNIT_SHADOWS] == 10000.f && others_game(out));
  CHECK(!next_frame_skips(MAP));
  std::printf("shadows only off: 16 = %.0f, next frame skipped 0\n", out[UNIT_SHADOWS]);

  // On, the rays' occlusion made this frame, the map known: 0, and the next frame skips the map's casters.
  out = present(true, true, GAME_DETAIL);
  CHECK(out[UNIT_SHADOWS] == 0.f && others_game(out));
  CHECK(next_frame_skips(MAP));
  CHECK(!next_frame_skips(OTHER_DEPTH));  // CONTROL
  std::printf("rays replace the map: 16 = %.0f, next frame skipped 1 (another depth target 0)\n", out[UNIT_SHADOWS]);

  // Held over frames while the rays keep tracing.
  for (int frame = 0; frame < 3; ++frame) {
    out = present(true, true, GAME_DETAIL);
    CHECK(out[UNIT_SHADOWS] == 0.f && others_game(out) && next_frame_skips(MAP));
  }

  // A frame whose sun draws made no rays' occlusion: the next frame draws the map's casters (OnBindTargets reads the frame
  // before's), and 16 is the game's at once; tracing again: 0 again.
  out = present(true, false, GAME_DETAIL);
  CHECK(out[UNIT_SHADOWS] == 10000.f && others_game(out) && !next_frame_skips(MAP));
  std::printf("occlusion stale: 16 = %.0f, next frame skipped 0\n", out[UNIT_SHADOWS]);
  out = present(true, true, GAME_DETAIL);
  CHECK(out[UNIT_SHADOWS] == 0.f && next_frame_skips(MAP));

  // The map not known (no sun draw learned it): the game's.
  scene::internal::shadow_map = 0;
  out = present(true, true, GAME_DETAIL);
  CHECK(out[UNIT_SHADOWS] == 10000.f && others_game(out));
  scene::internal::shadow_map = MAP;

  // Rays on again, then off: 0, then the game's at the next call.
  out = present(true, true, GAME_DETAIL);
  CHECK(out[UNIT_SHADOWS] == 0.f);
  out = present(false, true, GAME_DETAIL);
  CHECK(out[UNIT_SHADOWS] == 10000.f && others_game(out) && !next_frame_skips(MAP));
  std::printf("rays off again: 16 = %.0f\n", out[UNIT_SHADOWS]);

  // CONTROL: Unit Detail 2x on foot soldiers: index 0 alone doubled.
  out = present(false, true, {2.f, 1.f, 1.f, 1.f});
  CHECK(out[0] == 180.f && out[UNIT_SHADOWS] == 10000.f);
  for (size_t index = 1; index < out.size(); ++index) CHECK(out[index] == GAME[index]);
  std::printf("CONTROL Unit Detail 2x: 0 = %.0f (game 90)\n", out[0]);
  std::printf("PASS\n");
  return 0;
}
