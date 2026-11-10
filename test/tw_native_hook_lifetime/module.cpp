#include <windows.h>

#include <detours.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

static int fail_stage = 0;
static int detach_calls = 0;
static LONG WINAPI TestBegin() {
  return fail_stage == 1 ? ERROR_INVALID_OPERATION : DetourTransactionBegin();
}
static LONG WINAPI TestDetach(PVOID* original, PVOID replacement) {
  if (fail_stage == 2 && ++detach_calls == 2) return ERROR_INVALID_OPERATION;
  return DetourDetach(original, replacement);
}
static LONG WINAPI TestCommit() {
  if (fail_stage == 3) {
    DetourTransactionAbort();
    return ERROR_INVALID_OPERATION;
  }
  return DetourTransactionCommit();
}
#define DetourTransactionBegin  TestBegin
#define DetourDetach            TestDetach
#define DetourTransactionCommit TestCommit
#include "src/games/twattila/graphics_hooks.hpp"
#undef DetourTransactionBegin
#undef DetourDetach
#undef DetourTransactionCommit

namespace graphics = twattila::graphics_hooks::internal;
extern "C" __declspec(dllexport) int InstallTest(void* distances, void* scrub) {
  if (!graphics::hooks.empty()) return -1;
  graphics::original_unit_distances = reinterpret_cast<graphics::UnitDistances>(distances);
  graphics::original_draw_scrub = reinterpret_cast<graphics::DrawScrub>(scrub);
  graphics::hooks = {
      {&graphics::original_unit_distances, &graphics::OnUnitDistances},
      {&graphics::original_draw_scrub, &graphics::OnDrawScrub},
  };
  for (auto& scale : graphics::imposter_scales) scale.store(2.f);
  graphics::hook_lifetime.Install(graphics::hooks);
  graphics::hooks_active = true;
  return 1;
}
extern "C" __declspec(dllexport) int RemoveTest(int stage) {
  fail_stage = stage;
  detach_calls = 0;
  twattila::graphics_hooks::Uninstall();
  fail_stage = 0;
  return static_cast<int>(graphics::hooks.size());
}
extern "C" __declspec(dllexport) int ReactivateTest() {
  bool installed = false;
  twattila::graphics_hooks::Update({}, &installed, nullptr);
  return installed && graphics::hooks_active.load() && graphics::hooks.size() == 2;
}
#if !defined(_WIN64)
namespace lod = tw::engine_tweaks::lod_pick::internal;
extern "C" __declspec(dllexport) int InstallLod(void* target) {
  lod::original_pick_lod = reinterpret_cast<lod::PickLod>(target);
  lod::hooks = {{&lod::original_pick_lod, &lod::OnPickLod}};
  lod::hook_lifetime.Install(lod::hooks);
  lod::distance_squared_scale = 2.f;
  lod::hooks_active = true;
  return 1;
}
// As the game's engine values: in a module's image.
static float scaled_distance = 10.f;
static float reset_distance = 10.f;
static uint8_t forced_flag = 0;
// -1: the detach switched a value (the addon's stay, and one the game set again is the game's).
extern "C" __declspec(dllexport) int RemoveLod(int stage) {
  tw::engine_tweaks::Option option = {
      .scale = 2.f,
      .tweaks = {{.name = "scaled", .value = &scaled_distance},
                 {.name = "reset", .value = &reset_distance},
                 {.name = "flag", .flag = true, .value = &forced_flag}},
  };
  std::array<tw::engine_tweaks::Option*, 1> options = {&option};
  tw::engine_tweaks::internal::Apply(options);
  const bool applied = scaled_distance == 20.f && reset_distance == 20.f && forced_flag == 1;
  reset_distance = 15.f;
  fail_stage = stage;
  detach_calls = 1;  // The LOD owner has just one hook.
  tw::engine_tweaks::Uninstall(options);
  fail_stage = 0;
  if (!applied || scaled_distance != 20.f || reset_distance != 15.f || forced_flag != 1) return -1;
  scaled_distance = 10.f;
  reset_distance = 10.f;
  forced_flag = 0;
  return static_cast<int>(lod::hooks.size());
}
#endif
// The game's engine values are the host's (its image stays, this one unloads). Phase 0, the first image: two values
// scaled and a flag set, one value set again by the game, a constant in the game's code, then the detach. Phase 1, the
// image after it: the same sliders, then back at the game's. 0: as it must be.
extern "C" __declspec(dllexport) int EngineValues(float* scaled, float* reset, uint8_t* flag, float* constant, int phase) {
  tw::engine_tweaks::Option numbers = {
      .scale = 2.f,
      .tweaks = {{.name = "scaled", .value = scaled},
                 {.name = "reset", .value = reset},
                 {.code_pattern = "00 00 20 41", .value = constant}},
  };
  tw::engine_tweaks::Option flags = {.scale = 1.f, .tweaks = {{.name = "flag", .flag = true, .value = flag}}};
  std::array<tw::engine_tweaks::Option*, 2> options = {&numbers, &flags};
  if (phase == 0) {
    tw::engine_tweaks::internal::Apply(options);
    if (*scaled != 20.f || *reset != 20.f || *flag != 1 || *constant != 20.f) return 1;
    *reset = 15.f;
    tw::engine_tweaks::Uninstall(options);
    // Nothing is switched at the detach but the code constant, which the next image finds by the game's bytes.
    return (*scaled == 20.f && *reset == 15.f && *flag == 1 && *constant == 10.f) ? 0 : 2;
  }
  tw::engine_tweaks::internal::Restore(options);
  tw::engine_tweaks::internal::Apply(options);
  // Not scaled once more; the value the game set is its new base.
  if (*scaled != 20.f || *reset != 30.f || *flag != 1 || *constant != 20.f) return 3;
  numbers.scale = 1.f;
  flags.scale = 0.f;
  tw::engine_tweaks::internal::Apply(options);
  // The game's own, known across the unload.
  if (*scaled != 10.f || *reset != 15.f || *flag != 0 || *constant != 10.f) return 4;
  tw::engine_tweaks::Uninstall(options);
  return 0;
}
extern "C" __declspec(dllexport) constexpr const char* NAME = "Native hook lifetime test";
