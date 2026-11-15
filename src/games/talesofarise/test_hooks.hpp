/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "../../utils/log.hpp"

// The Unreal add-ons' scripted-run hooks: twattila/test_hooks.hpp's RENODX_TW_SET / SHOT (and SET's "trace" key, read by
// talesofarise_dlss/frame_trace.hpp), without the Total War ray-tracing scene: RENODX_TW_PROFILE has nothing to record.
namespace talesofarise::test_hooks {
inline void StartProfile() {
  renodx::utils::log::w("[Unreal] Test: RENODX_TW_PROFILE: no profile in this add-on.");
}
}  // namespace talesofarise::test_hooks

#define RENODX_TEST_HOOKS_PROFILE talesofarise::test_hooks::StartProfile
#include "../twattila/test_hooks.hpp"
