/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#if !defined(__clang__)
#include <xmmintrin.h>
#endif

namespace twattila {

// A read prefetch into every cache level (prefetcht0). clang-cl reads `_mm_prefetch`'s hint as a locality and the Windows
// SDK's `_MM_HINT_T0` (1) means prefetcht2 there: the builtin names the level itself.
inline void PrefetchRead(const void* address) {
#if defined(__clang__)
  __builtin_prefetch(address, 0, 3);
#else
  _mm_prefetch(static_cast<const char*>(address), _MM_HINT_T0);
#endif
}

}  // namespace twattila
