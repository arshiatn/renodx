/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila's anti-aliasing shader aa_dlaa_finish (twattila/anti_aliasing.hpp). Rome II's anti-aliasing takes hdr_to_screen's
// output (gamma 2.2 encoded, aa_dlaa_prepare): the square root back as Attila's.
#include "../twattila/aa_dlaa_finish.cs_5_0.hlsl"
