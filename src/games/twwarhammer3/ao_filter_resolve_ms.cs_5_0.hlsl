/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila's AO filter, Temporal Smoothing with the game's MSAA (twattila/ao_filter.hpp), reading the AO as this game's sun
// draws do.
#define AO_FILTER_SUN_MAPPING
#include "../twattila/ao_filter_resolve_ms.cs_5_0.hlsl"
