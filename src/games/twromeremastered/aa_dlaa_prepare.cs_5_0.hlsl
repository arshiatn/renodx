/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila's anti-aliasing shader aa_dlaa_prepare (dlaa.hpp). Rome Remastered's DLAA takes ResolveMainColor's output (FP16,
// gamma 2.2 encoded): squared as Attila's, as Rome II's.
#include "../twattila/aa_dlaa_prepare.cs_5_0.hlsl"
