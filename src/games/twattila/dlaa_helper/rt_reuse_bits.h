/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// PathConstants::reuse_tests' bits (tests: the RENODX_TW_REUSE_* switches, rt_d3d12.hpp; the bisection of ReSTIR GI's bias
// rising with the rays), the one list rt_path.hlsli and rt_d3d12.hpp include (C++: twattila::dlaa_helper's constants).
// ReuseTexel merges no neighbours (_NO_SPATIAL); PathTraceTexel validates no history (_NO_VALIDATE); ReuseTexel leaves Own
// Sky Rays' sky out (_NO_SKY); ContactShade 1 (_NO_CONTACT: CONTACT_RANGE 0, the raw paths' too); HitLight's
// SeenDiffuseLight picks keyed by the frame as well (_SEEN_LIGHTS_FRAME). PT DI step 2's suspects (G0, each its own switch):
// _SPATIAL_CLAMP the receiver-unit W clamp after the spatial Finish too; _SKY_DIRECTION a neighbour's sky sample (a point at
// its first ray's reach) moved as a direction; _NO_JACOBIAN_CUT no 0.1-10 cut; _HISTORY_PLAIN the history cap without the
// rays; _NEIGHBOUR_VISIBILITY a neighbour's sample shadow-tested before its merge (its target 0 when blocked);
// _JACOBIAN_SATURATE ReuseJacobian's cosines saturated (RTXDI_CalculateJacobian's: a sample whose surface faces away from
// the receiver 0, cut) instead of abs.
// RENODX_TW_REUSE_BIAS=1 / 2 (G1): the spatial merge's MIS-like normalization over the neighbours that could have made the
// chosen sample (Basic), they also seeing it (Raytraced, a ray each), instead of 1 / M (RTXDI's GI bias correction modes).
// RENODX_TW_REUSE_BIAS=3 (PT DI step 5's G2): pairwise MIS between each neighbour and the texel's own reservoir instead.
// Validate's parts (part C, the bias that grows with the rays): _VALIDATE_NO_DROP keeps a history whose sample's ray is blocked, _VALIDATE_KEEP_LIGHT keeps its stored light, _VALIDATE_NO_CUTS its count and weight, _VALIDATE_RESCALE takes
// the light now with its weight over the change (W x the target stays), _VALIDATE_NO_INVALIDITY returns no change (the
// history cap stays whole), _VALIDATE_KEEP_CHROMA takes the light now's luminance in the stored light's colour,
// _VALIDATE_NO_CACHE asks the screen only (no cache answer: the stored light stays); _NO_HISTORY_CLAMP: no ClampWeight after
// the history merge. _BOIL the boiling filter's pass (BoilTexel), _BOIL_PLANT its pass plants a texel 100 x too bright
// first; _RTXDI RTXDI's spatial resampling in the reuse's place (the reference build; the host's only).
// Balanced ReSTIR Light (bridge RT_PATH_REUSE_BALANCED, the player's row): REUSE_BIAS_BASIC in its REUSE_BALANCED reuse builds,
// _VALIDATE_KEEP_LIGHT set by the host: the only one the player's paths read (rt_path.hlsli REUSE_SUSPECTS).
#ifndef SRC_GAMES_TWATTILA_DLAA_HELPER_RT_REUSE_BITS_H_
#define SRC_GAMES_TWATTILA_DLAA_HELPER_RT_REUSE_BITS_H_

#if defined(__cplusplus)
#include <cstdint>

namespace twattila::dlaa_helper {
#define RT_REUSE_BIT inline constexpr uint32_t
#else
#define RT_REUSE_BIT static const uint
#endif
RT_REUSE_BIT REUSE_TEST_NO_SPATIAL = 1u;
RT_REUSE_BIT REUSE_TEST_NO_VALIDATE = 2u;
RT_REUSE_BIT REUSE_TEST_NO_SKY = 4u;
RT_REUSE_BIT REUSE_TEST_NO_CONTACT = 8u;
RT_REUSE_BIT REUSE_TEST_SEEN_LIGHTS_FRAME = 16u;
RT_REUSE_BIT REUSE_TEST_SPATIAL_CLAMP = 32u;
RT_REUSE_BIT REUSE_TEST_SKY_DIRECTION = 64u;
RT_REUSE_BIT REUSE_TEST_NO_JACOBIAN_CUT = 128u;
RT_REUSE_BIT REUSE_TEST_HISTORY_PLAIN = 256u;
RT_REUSE_BIT REUSE_TEST_NEIGHBOUR_VISIBILITY = 512u;
RT_REUSE_BIT REUSE_BIAS_BASIC = 1024u;
RT_REUSE_BIT REUSE_BIAS_RAYTRACED = 2048u;
RT_REUSE_BIT REUSE_TEST_JACOBIAN_SATURATE = 4096u;
RT_REUSE_BIT REUSE_TEST_BOIL = 8192u;
RT_REUSE_BIT REUSE_TEST_BOIL_PLANT = 16384u;
RT_REUSE_BIT REUSE_TEST_RTXDI = 32768u;
RT_REUSE_BIT REUSE_TEST_VALIDATE_NO_DROP = 65536u;
RT_REUSE_BIT REUSE_TEST_VALIDATE_KEEP_LIGHT = 131072u;
RT_REUSE_BIT REUSE_TEST_VALIDATE_NO_CUTS = 262144u;
RT_REUSE_BIT REUSE_TEST_VALIDATE_RESCALE = 524288u;
RT_REUSE_BIT REUSE_TEST_VALIDATE_NO_INVALIDITY = 1048576u;
RT_REUSE_BIT REUSE_TEST_VALIDATE_KEEP_CHROMA = 2097152u;
RT_REUSE_BIT REUSE_TEST_NO_HISTORY_CLAMP = 4194304u;
RT_REUSE_BIT REUSE_TEST_VALIDATE_NO_CACHE = 8388608u;
RT_REUSE_BIT REUSE_BIAS_PAIRWISE = 16777216u;
#undef RT_REUSE_BIT
#if defined(__cplusplus)
}  // namespace twattila::dlaa_helper
#endif
// PT DI step 5's G4 (a build option, 0: off): ReSTIR GI's and Own ReSTIR's reservoirs in 24 bytes a texel, not 32 (the
// hit's position and sqrt(W) as f16 | M as R32G32B32A32_UINT; the light RGB9E5 and both normals 8 + 8 octahedral as R32G32_UINT).
#define RT_RESERVOIR_COMPACT 0

#endif  // SRC_GAMES_TWATTILA_DLAA_HELPER_RT_REUSE_BITS_H_
