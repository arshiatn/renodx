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
// A PickLod of the game's first 50 bytes (the pattern in twrome2/engine_tweaks.hpp, which Update finds in this module), then
// a tail for a model without LODs (+0x28 is 0): the camera's distance squared as the result.
#pragma section(".text", read, execute)
alignas(16) __declspec(allocate(".text")) static const uint8_t fake_pick_lod[] = {
    0x53,                                      // push ebx
    0x55,                                      // push ebp
    0x8B, 0x6C, 0x24, 0x10,                    // mov ebp, [esp + 0x10]: next
    0x56,                                      // push esi
    0x57,                                      // push edi
    0x8B, 0xF9,                                // mov edi, ecx: model
    0xC7, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00,  // mov dword ptr [ebp], 0
    0x8B, 0x47, 0x28,                          // mov eax, [edi + 0x28]: the LOD count
    0x85, 0xC0,                                // test eax, eax
    0x0F, 0x84, 0x1C, 0x00, 0x00, 0x00,        // je tail
    0x83, 0xF8, 0x01,                          // cmp eax, 1
    0x75, 0x11,                                // jne
    0x8B, 0x47, 0x2C,                          // mov eax, [edi + 0x2c]
    0x8B, 0x00,                                // mov eax, [eax]
    0x83, 0x78, 0x20, 0x00,                    // cmp dword ptr [eax + 0x20], 0
    0x75, 0x06,                                // jne
    0x83, 0x78, 0x2C, 0x00,                    // cmp dword ptr [eax + 0x2c], 0
    0x77, 0x7A,                                // ja
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,        // mov ecx, [address]
    0x8B, 0x44, 0x24, 0x14,                    // tail: mov eax, [esp + 0x14]: the distance squared
    0x5F, 0x5E, 0x5D, 0x5B,                    // pop edi, esi, ebp, ebx
    0xC2, 0x08, 0x00,                          // ret 8
};
extern "C" __declspec(dllexport) const uint8_t* FakePickLod() { return fake_pick_lod; }
// What the add-on's present does with the slider at `scale` (the first one finds the module). Bit 0: a hook is installed,
// 1: it is active, 2: the restart note shows. The status text through `status_text`.
static tw::engine_tweaks::Option lod_slider;
static bool lod_found = false;
static renodx::utils::settings::Setting lod_status;
static renodx::utils::settings::Setting* lod_note = tw::engine_tweaks::RestartNote(&lod_slider);
static std::array<tw::engine_tweaks::Option*, 1> lod_options = {&lod_slider};
extern "C" __declspec(dllexport) int UpdateLod(float scale, const char** status_text) {
  lod_slider.scale = scale;
  tw::engine_tweaks::Update(L"native_hooks_test.dll", lod_options, &lod_slider, &lod_found, &lod_status);
  *status_text = lod_status.label.c_str();
  return (lod::hooks.empty() ? 0 : 1) | (lod::hooks_active.load() ? 2 : 0) | (lod_note->is_visible() ? 4 : 0);
}
// The add-on's detach and, after it, a new image's first look.
extern "C" __declspec(dllexport) int DetachLod() {
  tw::engine_tweaks::Uninstall(lod_options);
  lod_found = false;
  return static_cast<int>(lod::hooks.size());
}
#endif
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
