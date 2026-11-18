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
// The game's engine values are the host's (its image stays, this one unloads). Phase 0, the first image: two values
// scaled and a flag set, one value set again by the game, a constant in the game's code, then the detach. Phase 1, the
// image after it: the same sliders, its detach, the image after that, then back at the game's. Phase 2, an image that
// saw no frame: only its detach. 0: as it must be.
extern "C" __declspec(dllexport) int EngineValues(float* scaled, float* reset, uint8_t* flag, float* constant, int phase) {
  tw::engine_tweaks::Option numbers = {
      .scale = 2.f,
      .tweaks = {{.name = "scaled", .value = scaled},
                 {.name = "reset", .value = reset},
                 {.code_pattern = "00 00 20 41", .value = constant}},
  };
  tw::engine_tweaks::Option flags = {.scale = 1.f, .tweaks = {{.name = "flag", .flag = true, .value = flag}}};
  std::array<tw::engine_tweaks::Option*, 2> options = {&numbers, &flags};
  tw::engine_tweaks::Option next_numbers = numbers;
  tw::engine_tweaks::Option next_flags = flags;
  std::array<tw::engine_tweaks::Option*, 2> next_options = {&next_numbers, &next_flags};
  if (phase == 2) {
    // It knows none of the values: what the image before kept stays (phase 1 then finds it).
    tw::engine_tweaks::Uninstall(options);
    return (*scaled == 20.f && *flag == 1) ? 0 : 7;
  }
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
  // Its detach with the sliders on: the kept values updated by their addresses, the one the game had set added.
  tw::engine_tweaks::Uninstall(options);
  if (*scaled != 20.f || *reset != 30.f || *flag != 1 || *constant != 10.f) return 5;
  tw::engine_tweaks::internal::Restore(next_options);
  tw::engine_tweaks::internal::Apply(next_options);
  if (*scaled != 20.f || *reset != 30.f || *flag != 1 || *constant != 20.f) return 6;
  next_numbers.scale = 1.f;
  next_flags.scale = 0.f;
  tw::engine_tweaks::internal::Apply(next_options);
  // The game's own, known across the unloads.
  if (*scaled != 10.f || *reset != 15.f || *flag != 0 || *constant != 10.f) return 4;
  tw::engine_tweaks::Uninstall(next_options);
  return 0;
}
extern "C" __declspec(dllexport) constexpr const char* NAME = "Native hook lifetime test";
