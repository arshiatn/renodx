/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila's anti-aliasing shader aa_dlaa_prepare (twattila/anti_aliasing.hpp). Rome II's anti-aliasing takes hdr_to_screen's
// output (the tone mapper EE1DA45C draws into R11G11B10, which it skips), gamma 2.2 encoded: squared as Attila's.
#include "../twattila/aa_dlaa_prepare.cs_5_0.hlsl"
