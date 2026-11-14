/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Shogun 2 ray tracing: the leaves' shadow pass (rigidleafwet_v3 5851CA62) as rt_capture_cascade_tree, a fully dissolved tree's leaves
// left out (its branches' pixel shader D607E06E reads no fade: their version keeps them).
#define RT_TREE_FADE
#include "./rt_capture_cascade_tree.vs_5_0.hlsl"
