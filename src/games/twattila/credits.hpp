/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

// About's lines for third-party work shared by several add-ons (their own lines stay in each add-on).
namespace twattila::credits {

// What ships with the Total War helper (renodx-tw-dlaa64.exe; Shogun 2's, Ryse's and Expeditions: Rome's ray tracing too).
constexpr const char* HELPER =
    "NVIDIA DLSS (Super Resolution, Ray Reconstruction and Frame Generation), NVIDIA Reflex, Streamline, "
    "NVAPI, the Opacity Micro-Map SDK, SHaRC and NRD: NVIDIA Corporation, under NVIDIA's licenses (this software "
    "contains source code provided by NVIDIA Corporation). The Nsight Graphics SDK: NVIDIA Corporation (Apache "
    "License 2.0). NVIDIA, GeForce "
    "RTX, DLSS and Reflex are trademarks and/or registered trademarks of NVIDIA Corporation. DirectX "
    "Agility SDK: Microsoft. Ray tracing: ideas from Embark Studios' kajiya (MIT) and Bustanity's "
    "Phantom FX (MIT). AMD FSR: Advanced Micro Devices, Inc. (MIT License). XeSS: Intel Corporation (Intel "
    "Simplified Software License). Every license ships with the mod.";

// The DLSS add-ons (talesofarise_dlss's frame generation, shared by Ryse's, Expeditions: Rome's and Shogun 2's).
constexpr const char* DLSS_ADDON =
    "The DLSS add-on: NVIDIA DLSS (the NGX SDK, under the NVIDIA RTX SDKs license) and Streamline (MIT): NVIDIA "
    "Corporation (this software contains source code provided by NVIDIA Corporation); NVIDIA and DLSS are trademarks "
    "and/or registered trademarks of NVIDIA Corporation. Frame generation's D3D12 device after Bustanity's Phantom FX "
    "(MIT). Hooks: Microsoft Detours (MIT). Every license ships with the mod.";

// The ACES reference rendering transform (Tales of Arise's and Expeditions: Rome's UE Extended).
constexpr const char* ACES =
    "ACES: Copyright (C) 2015 Academy of Motion Picture Arts and Sciences (A.M.P.A.S.). Portions contributed by others "
    "as indicated. All rights reserved. License and disclaimer: ACES_LICENSE.md.";

}  // namespace twattila::credits
