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
// 5. Small Soldiers as Sprites (Scales::sprite_pixels, anti_aliasing::MainFocalPixels) through the ImposterDistance hook over
//    a stand-in for the game's (the size's distance + (random - 0.5) x its fade width, from the 18 the display holds):
//    off, or no camera yet: the game's bits; on: no farther than where a soldier of its size is N pixels tall (h x focal /
//    d = N at random 0.5), the game's spread kept, nearer game distances untouched, sizes past 3 as 0; printed: where
//    each size switches at 4K with a 40 degree vertical field of view.
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
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

// What the display copied from UnitDistances (+0x234), and the game's ImposterDistance over it (+0x48a8f0: past 3 the
// jump table's default takes size 0's).
static std::array<float, 18> display_distances = GAME;

static float __fastcall GameImposterDistance(void*, void*, uint32_t size, float random) {
  const uint32_t index = (size <= 3 ? size : 0);
  return (random - 0.5f) * display_distances[4 + index] + display_distances[index];
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

  // 5. Small Soldiers as Sprites.
  hooks::internal::original_imposter_distance = &GameImposterDistance;
  auto& aa = twattila::anti_aliasing::internal::runtime;
  CHECK(twattila::anti_aliasing::MainFocalPixels() == 0.f);  // no main camera yet
  constexpr float TAN_HALF_FOV = 0.36397023f;                // tan(20 degrees): 40 degrees vertical
  aa.display_height = 2160;
  aa.camera.projection[5] = 1.f / TAN_HALF_FOV;
  const float focal = twattila::anti_aliasing::MainFocalPixels();
  CHECK(std::abs(focal - 2160.f / (2.f * TAN_HALF_FOV)) < 0.01f);
  const auto settle = [&](const std::array<float, 4>& unit_detail, float pixels, float focal_pixels) {
    hooks::Update({.unit_detail = unit_detail, .sprite_pixels = pixels, .focal_pixels = focal_pixels}, &installed, &status);
    hooks::internal::OnUnitDistances(nullptr, nullptr, display_distances.data(), 0, 0, 0, 0);
  };
  constexpr std::array<float, 9> RANDOMS = {0.f, 0.01f, 0.25f, 0.4999f, 0.5f, 0.5001f, 0.75f, 0.99f, 1.f};
  constexpr std::array<uint32_t, 6> SIZES = {0, 1, 2, 3, 4, 7};
  const auto game = [](uint32_t size, float random) { return GameImposterDistance(nullptr, nullptr, size, random); };
  const auto hooked = [](uint32_t size, float random) { return hooks::internal::OnImposterDistance(nullptr, nullptr, size, random); };
  const auto bits = [](float value) { return std::bit_cast<uint32_t>(value); };
  // Off (0 px), and on without a camera (focal 0): the game's bits, at 1x and 8x.
  for (const float detail : {1.f, 8.f}) {
    for (const auto& [pixels, focal_pixels] : {std::pair{0.f, focal}, std::pair{20.f, 0.f}}) {
      settle({detail, detail, detail, detail}, pixels, focal_pixels);
      for (const uint32_t size : SIZES) {
        for (const float random : RANDOMS) CHECK(bits(hooked(size, random)) == bits(game(size, random)));
      }
    }
  }
  // On at 8x (the game's sprites at 720 / 880 / 1600 / 8000 m): each size's switch where it is 20 px tall, the spread kept.
  settle({8.f, 8.f, 8.f, 8.f}, 20.f, focal);
  std::array<float, 4> switch_at = {};
  for (const uint32_t size : SIZES) {
    const float height = hooks::internal::SOLDIER_HEIGHTS[size <= 3 ? size : 0];
    const float centre = hooked(size, 0.5f);
    CHECK(std::abs(height * focal / centre - 20.f) < 1e-4f * 20.f);  // N pixels tall at the switch
    CHECK(centre < game(size, 0.5f));                                 // CONTROL: the cap took it
    for (const float random : RANDOMS) {
      const float spread = game(size, random) - game(size, 0.5f);
      CHECK(std::abs((hooked(size, random) - centre) - spread) < 1e-3f);
      CHECK(hooked(size, random) <= game(size, random));
    }
    if (size <= 3) switch_at[size] = centre;
  }
  CHECK(bits(hooked(7, 0.3f)) == bits(hooked(0, 0.3f)));  // past 3: size 0's
  // Nearer than the cap (Unit Detail 0.5x: 45 m foot soldiers): the game's bits.
  settle({0.5f, 0.5f, 0.5f, 0.5f}, 20.f, focal);
  for (const float random : RANDOMS) CHECK(bits(hooked(0, random)) == bits(game(0, random)));
  // More pixels, nearer sprites.
  settle({8.f, 8.f, 8.f, 8.f}, 40.f, focal);
  CHECK(std::abs(hooked(0, 0.5f) - switch_at[0] * 0.5f) < 1e-3f * switch_at[0]);
  std::printf("small soldiers as sprites, 4K at 40 degrees (focal %.1f px), 20 px: foot soldiers %.0f m, cavalry %.0f m, "
              "elephants %.0f m, artillery %.0f m (8x: 720 / 880 / 1600 / 8000 m); off and without a camera the game's "
              "bits\n",
              focal, switch_at[0], switch_at[1], switch_at[2], switch_at[3]);
  settle({1.f, 1.f, 1.f, 1.f}, 0.f, 0.f);
  std::printf("PASS\n");
  return 0;
}
