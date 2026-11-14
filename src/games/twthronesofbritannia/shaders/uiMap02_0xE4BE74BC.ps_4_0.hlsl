/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// sprite.hlsl ps untextured_pixel (the UI's plain coloured sprites), the same bytes as Attila's: Attila's uiMap02, the
// UNORM clamp back and the vertex colour (made for a 2.2 screen) re-encoded for the gamma 2.0 frame (this game's
// shared.h first: Attila's include in the file is then skipped, the same guard).
#include "../shared.h"
#include "../../twattila/shaders/uiMap02_0xE4BE74BC.ps_4_0.hlsl"
