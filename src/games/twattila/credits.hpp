/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <format>
#include <string>
#include <utility>

// About's lines for third-party work shared by several add-ons (their own lines stay in each add-on), and the red line
// every HDR add-on shows when it runs alone (no renodx-tw-dlaa64.exe next to it).
namespace twattila::credits {

// What a game ships of the methods several add-ons share (Methods' line names only those).
struct Shipped {
  bool psycho_v30 = true;       // PsychoV30 (else the line names PsychoV31 and PsychoV35 alone)
  bool gtvbao = false;          // Visibility Bitmask ambient occlusion
  bool gtvbgi = false;          // Visibility Bitmask Light Bounce
  bool auto_thickness = false;  // their Auto Thickness rows
  bool ue_extended = false;     // UE Filmic Extended
  bool bend = false;            // Bend screen-space shadows
  bool smaa = false;
  bool cmaa2 = false;
  bool sssr = false;     // the reflections' FidelityFX SSSR ray march
  bool detours = false;  // hooks through Microsoft Detours
};

// About's line of those methods, with their authors and licences (Attila's texts; a game's own lines follow it).
inline std::string Methods(const Shipped& shipped) {
  std::string text = std::format("RenoDX, {}PsychoV31, PsychoV35 and Hue Clip Emulation by ShortFuse. This mod uses Musa's custom "
                                 "PsychoV31 and PsychoV35.",
                                 (shipped.psycho_v30 ? "PsychoV30, " : ""));
  if (shipped.gtvbao || shipped.gtvbgi) {
    const bool both = shipped.gtvbao && shipped.gtvbgi;
    text += std::format(" Visibility Bitmask {}: the method of Therrien, Levesque and Gilet (2023) on GTAO (Jimenez et al. 2016), as "
                        "{} by Mirko Salm (2024){}.",
                        (both ? "ambient occlusion and Light Bounce" : (shipped.gtvbao ? "ambient occlusion" : "Light Bounce")),
                        (both ? "GT-VBAO and GT-VBGI" : (shipped.gtvbao ? "GT-VBAO" : "GT-VBGI")),
                        (shipped.auto_thickness ? "; auto thickness after Bjorn Ottosson" : ""));
  }
  if (shipped.ue_extended) text += " UE Filmic Extended by Marat (marat569), from RenoDX's UE Extended add-on, MIT License.";
  if (shipped.bend) text += "\nScreen-space shadows: Bend Studio, Copyright 2023 Sony Interactive Entertainment, Apache License 2.0.";
  std::string libraries;
  for (const auto& [on, line] : {
           std::pair{shipped.smaa, "SMAA: Jimenez, Echevarria, Masia, Navarro and Gutierrez (2013), MIT License."},
           std::pair{shipped.cmaa2, "CMAA2: Filip Strugar, Copyright (c) 2018 Intel Corporation, Apache License 2.0."},
           std::pair{shipped.sssr, "Reflections' ray march: AMD FidelityFX SSSR, Copyright (c) 2021 Advanced Micro Devices, MIT License."},
           std::pair{shipped.detours, "Hooks: Microsoft Detours, Copyright (c) Microsoft Corporation, MIT License."},
       }) {
    if (on) libraries += (libraries.empty() ? "" : " ") + std::string(line);
  }
  if (!libraries.empty()) text += "\n" + libraries;
  return text;
}

// The RenoDX line of the add-ons with PsychoV30 and PsychoV35 (both Divinity games).
constexpr const char* RENODX_PSYCHO_V30_V35 =
    "RenoDX, PsychoV30, PsychoV35 and Hue Clip Emulation by ShortFuse. This mod uses Musa's custom PsychoV30 and "
    "PsychoV35.";

constexpr const char* FULL_DOWNLOAD_NOTE = "DLSS, FSR, XeSS, Frame Generation and ray tracing need the full download from GitHub: github.com/arshiatn/renodx/releases";

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
