// The second module of tests.cpp's case 8: its own command_action and fast dispatch over the same process-shared
// registrations, as the second add-on of a Total War pair has (Shogun 2's HDR + DLSS). Built as
// fast_draws_partner_<arch>.dll; its ReShade calls reach the test exe's mocked exports. Never unloaded by the test.
#include <windows.h>

#include "src/utils/command_action_fast.hpp"

namespace action = renodx::utils::command_action;

static void (*log_line)(const char* line) = nullptr;

// Command-wide and ungated, as resource_upgrade's on_render_pass_command in an add-on.
constexpr auto PARTNER_COMMAND = []<typename Context>(Context&) -> action::CallbackResult<Context> {
  log_line("partner");
  return {};
};

// As an add-on's AttachAddon: its callbacks, command_action, then the opt-in.
extern "C" __declspec(dllexport) void PartnerAttach(void (*log)(const char* line)) {
  log_line = log;
  action::Register(PARTNER_COMMAND, {.command_types = action::COMMAND_TYPE_DRAW_INDEXED});
  action::Use(DLL_PROCESS_ATTACH);
  action::fast::Use(DLL_PROCESS_ATTACH);
}

// As an add-on's DetachAddon: the opt-in, then command_action.
extern "C" __declspec(dllexport) void PartnerDetach() {
  action::fast::Use(DLL_PROCESS_DETACH);
  action::Use(DLL_PROCESS_DETACH);
}

// After its detach, as resource_upgrade's Unregister in swapchain::Use(DLL_PROCESS_DETACH).
extern "C" __declspec(dllexport) void PartnerUnregister() {
  action::Unregister(PARTNER_COMMAND);
}
