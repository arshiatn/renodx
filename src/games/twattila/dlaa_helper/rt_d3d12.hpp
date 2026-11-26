/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include <numeric>
#include <source_location>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../dlaa_bridge.h"
#include "./gpu_completion.hpp"
#include "./rt_reuse_bits.h"  // PathConstants::reuse_tests' bits (tests; rt_path.hlsli)
// NVIDIA's NVAPI (MIT; build.ps1 adds it when build\downloads\nvapi holds it): RTX Mega Geometry's cluster operations.
#if __has_include(<nvapi.h>)
#include <nvapi.h>
#define RENODX_NVAPI_CLUSTERS 1
#endif
// NVIDIA's Opacity Micro-Map SDK (NVIDIA RTX SDKs License; build.ps1 adds its header when build\downloads\omm holds its
// Windows release): the leaves' opacity micromaps baked on the CPU (its omm-lib.dll next to the helper, loaded at run
// time; without it, none).
#if __has_include(<omm.h>)
#include <omm.h>
#define RENODX_OMM_SDK 1
#endif
#include "./nrd_d3d12.hpp"
#if defined(RENODX_NRD)
#include "rt_nrd_finish_AMBIENT.h"  // dxc -Fh (build.ps1)
#include "rt_nrd_finish_BOUNCE.h"   // dxc -Fh (build.ps1)
#include "rt_nrd_finish_REFLECT.h"  // dxc -Fh (build.ps1)
#include "rt_nrd_finish_SUN.h"      // dxc -Fh (build.ps1)
#include "rt_nrd_front_AMBIENT.h"   // dxc -Fh (build.ps1)
#include "rt_nrd_front_BOUNCE.h"    // dxc -Fh (build.ps1)
#include "rt_nrd_front_REFLECT.h"   // dxc -Fh (build.ps1)
#include "rt_nrd_front_SUN.h"       // dxc -Fh (build.ps1)
#endif
#include "rt_bounce.h"            // dxc -Fh (build.ps1)
#include "rt_bounce_omm.h"        // dxc -Fh (build.ps1)
#include "rt_cache_fill.h"        // dxc -Fh (build.ps1)
#include "rt_cache_resolve.h"     // dxc -Fh (build.ps1)
#include "rt_compact.h"           // dxc -Fh (build.ps1)
#include "rt_fog.h"               // dxc -Fh (build.ps1)
#include "rt_lights.h"            // dxc -Fh (build.ps1)
#include "rt_lights_omm.h"        // dxc -Fh (build.ps1)
#include "rt_lights_bounce.h"     // dxc -Fh (build.ps1)
#include "rt_lights_bounce_omm.h" // dxc -Fh (build.ps1)
#include "rt_fog_omm.h"           // dxc -Fh (build.ps1)
#include "rt_path.h"              // dxc -Fh (build.ps1)
#include "rt_path_omm.h"          // dxc -Fh (build.ps1)
#include "rt_path_one.h"          // dxc -Fh (build.ps1)
#include "rt_path_one_omm.h"      // dxc -Fh (build.ps1)
#include "rt_path_ser.h"          // dxc -Fh (build.ps1)
#include "rt_path_ser_one.h"      // dxc -Fh (build.ps1)
#include "rt_place.h"             // dxc -Fh (build.ps1)
#include "rt_omm_uvs.h"           // dxc -Fh (build.ps1)
#include "rt_reflect.h"           // dxc -Fh (build.ps1)
#include "rt_reflect_filter.h"    // dxc -Fh (build.ps1)
#include "rt_reflect_omm.h"       // dxc -Fh (build.ps1)
#include "rt_reflect_one.h"       // dxc -Fh (build.ps1)
#include "rt_reflect_one_omm.h"   // dxc -Fh (build.ps1)
#include "rt_reuse.h"             // dxc -Fh (build.ps1)
#include "rt_reuse_omm.h"         // dxc -Fh (build.ps1)
#include "rt_reuse_lights.h"      // dxc -Fh (build.ps1)
#include "rt_reuse_lights_omm.h"  // dxc -Fh (build.ps1)
#include "rt_reuse_lean.h"        // dxc -Fh (build.ps1)
#include "rt_reuse_lean_omm.h"    // dxc -Fh (build.ps1)
#include "rt_reuse_near.h"        // dxc -Fh (build.ps1)
#include "rt_reuse_near_omm.h"    // dxc -Fh (build.ps1)
#include "rt_reuse_balanced.h"              // dxc -Fh (build.ps1)
#include "rt_reuse_balanced_omm.h"          // dxc -Fh (build.ps1)
#include "rt_reuse_lights_balanced.h"       // dxc -Fh (build.ps1)
#include "rt_reuse_lights_balanced_omm.h"   // dxc -Fh (build.ps1)
#include "rt_reuse_lean_balanced.h"         // dxc -Fh (build.ps1)
#include "rt_reuse_lean_balanced_omm.h"     // dxc -Fh (build.ps1)
#include "rt_reuse_near_balanced.h"         // dxc -Fh (build.ps1)
#include "rt_reuse_near_balanced_omm.h"     // dxc -Fh (build.ps1)
#include "rt_reuse_balanced_raytraced.h"              // dxc -Fh (build.ps1)
#include "rt_reuse_balanced_raytraced_omm.h"          // dxc -Fh (build.ps1)
#include "rt_reuse_lights_balanced_raytraced.h"       // dxc -Fh (build.ps1)
#include "rt_reuse_lights_balanced_raytraced_omm.h"   // dxc -Fh (build.ps1)
#include "rt_reuse_lean_balanced_raytraced.h"         // dxc -Fh (build.ps1)
#include "rt_reuse_lean_balanced_raytraced_omm.h"     // dxc -Fh (build.ps1)
#include "rt_reuse_near_balanced_raytraced.h"         // dxc -Fh (build.ps1)
#include "rt_reuse_near_balanced_raytraced_omm.h"     // dxc -Fh (build.ps1)
#include "rt_reflect_reservoir.h"      // dxc -Fh (build.ps1)
#include "rt_reflect_reservoir_omm.h"  // dxc -Fh (build.ps1)
#include "rt_scene_data.h"        // dxc -Fh (build.ps1)
#include "rt_soft.h"              // dxc -Fh (build.ps1)
#include "rt_sway.h"              // dxc -Fh (build.ps1)
#include "rt_trace.h"             // dxc -Fh (build.ps1)
#include "rt_trace_omm.h"         // dxc -Fh (build.ps1)
// NVIDIA's SHaRC (NVIDIA RTX SDKs License; build.ps1 compiles its headers into these when build\downloads\sharc holds
// them): SHaRC Radiance Cache.
#if __has_include("rt_path_sharc.h")
#include "rt_path_sharc.h"
#include "rt_path_sharc_one.h"
#include "rt_sharc_resolve.h"
#include "rt_sharc_update.h"
#define RENODX_SHARC 1
#endif
// NVIDIA's RTXDI-Library 3.2 (NVIDIA RTX SDKs License; build.ps1 compiles its headers into these when build\downloads\rtxdi
// holds its copy): ReSTIR GI's boiling filter (RENODX_TW_REUSE_BOIL, tests).
#if __has_include("rt_reuse_boil.h")
#include "rt_reuse_boil.h"
#define RENODX_RTXDI 1
#endif
// Its ReGIR for Torch Light in Path-Traced GI (PT DI step 3, T2; build.ps1 adds the copy's C++ with the build): the cells'
// build (rt_regir_presample) and the onion's parameters (ReGIR.cpp); rt_lights' twins with T1 (TORCH_IRRADIANCE).
#if __has_include("rt_regir_presample.h")
#include <Rtxdi/LightSampling/RISBufferSegmentAllocator.h>
#include <Rtxdi/ReGIR/ReGIR.h>

#include "rt_lights_bounce_torch.h"
#include "rt_lights_bounce_torch_omm.h"
#include "rt_lights_torch.h"
#include "rt_lights_torch_omm.h"
#include "rt_regir_presample.h"
#if defined(RENODX_SHARC)
#include "rt_sharc_update_regir.h"
#endif
#define RENODX_REGIR 1
#endif
// Matched ReSTIR Neighbours (RT_RESTIR_COMPATIBLE, RENODX_TW_REUSE_COMPATIBLE): the reuse's REUSE_COMPATIBLE builds (RTXDI's
// neighbour compatibility score) and their Balanced ReSTIR Light twins (Basic, Raytraced).
#if __has_include("rt_reuse_compatible.h")
#include "rt_reuse_compatible.h"
#include "rt_reuse_compatible_balanced.h"
#include "rt_reuse_compatible_balanced_omm.h"
#include "rt_reuse_compatible_balanced_raytraced.h"
#include "rt_reuse_compatible_balanced_raytraced_omm.h"
#include "rt_reuse_compatible_omm.h"
#define RENODX_REUSE_COMPATIBLE 1
#endif
// RENODX_TW_PATH_PROBE (tests only: build.ps1 compiles these with RENODX_TW_PATH_PROBE_BUILD set): rt_path.hlsli ProbeTexel.
#if __has_include("rt_path_probe.h")
#include "rt_path_probe.h"
#include "rt_path_probe_omm.h"
#define RENODX_PATH_PROBE 1
#endif
// RENODX_TW_REFERENCE (tests only: build.ps1 compiles these with RENODX_TW_REFERENCE_BUILD set): rt_reference.hlsli's passes.
#if __has_include("rt_reference_paths.h")
#include "rt_reference_accumulate.h"
#include "rt_reference_census.h"
#include "rt_reference_digest.h"
#include "rt_reference_group.h"
#include "rt_reference_paths.h"
#include "rt_reference_reflect.h"
#include "rt_reference_state.h"
#define RENODX_REFERENCE 1
#endif
// ReSTIR GI's test switches' builds (REUSE_TESTS, tests only: build.ps1 compiles these with RENODX_TW_REFERENCE_BUILD set;
// without them those switches do nothing).
#if defined(RENODX_REFERENCE) && __has_include("rt_path_tests.h")
#include "rt_path_tests.h"
#include "rt_path_tests_omm.h"
#include "rt_reuse_tests.h"
#include "rt_reuse_tests_omm.h"
#define RENODX_REUSE_TESTS 1
#endif
// The reference build's RTXDI GI contest (RENODX_TW_REUSE_RTXDI, tests; build.ps1 compiles these with RENODX_TW_REFERENCE_BUILD
// set and the RTXDI copy found): rt_path.hlsli RtxdiPackTexel, ReuseTexel with RTXDI's spatial resampling.
#if defined(RENODX_REFERENCE) && __has_include("rt_reuse_rtxdi.h")
#include "rt_reuse_rtxdi.h"
#include "rt_reuse_rtxdi_omm.h"
#include "rt_reuse_rtxdi_pack.h"
#define RENODX_RTXDI_GI 1
#endif
// The bridge recorder (tests only: build.ps1 writes rt_recorder_build.h with RENODX_TW_RECORDER_BUILD set): RENODX_TW_RECORD's
// snapshot and recorded requests, the "play" command's player (main.cpp; rt_recording.h).
#if __has_include("rt_recorder_build.h")
#include <cstdio>
#include <functional>
#include <optional>
#include <tuple>

#include "./rt_recording.h"
#include "./rt_uses.h"
#include "rt_burn.h"     // dxc -Fh (build.ps1, recorder builds)
#include "rt_moments.h"  // dxc -Fh (build.ps1, recorder builds)
#define RENODX_RECORDER 1
#endif

// The DLAA helper's ray tracing (bridge v4-v6; the addon's rt_scene.hpp): DXR needs a 64-bit process, the Total War
// games with the bridge are 32-bit. Its own D3D12 device keeps a BLAS per captured draw (its triangles in world space,
// copied from the shared vertex texture when the addon captures it; compacted once the GPU says how small it can be),
// refits the frame's units into one BLAS (rebuilt when their draws change, or every UNITS_REBUILD_FRAMES), builds the
// TLAS of the meshes the frame drew (and, but for the terrain, those drawn within RECENT_FRAMES: casters behind the
// camera) with it, and traces rt_trace.cs_6_5.hlsl into the shared visibility textures (soft shadows through
// rt_soft.cs_6_5.hlsl). Placed meshes (bridge v14: rigid objects and trees, keyed by their geometry) have a BLAS from their
// first instance, placed on the GPU (rt_place.cs_6_5.hlsl) where each frame's draws put their instances, each instance
// kept a while after its last draw (off-screen casters). Leaves are alpha-tested against their opacity masks (the addon's atlas, v6): each geometry has a
// record (its instance's InstanceID plus its geometry index) with its UVs and normals in pools (rt_scene_data.cs_6_5.hlsl).
// With the Agility SDK next to the helper (D3D12\D3D12Core.dll) and shader model 6.9, DXR 1.2: the leaves get opacity
// micromaps (baked by NVIDIA's OMM SDK, omm-lib.dll next to the helper; AdvanceMicromaps). With a bridge v7 addon's
// ray-traced Light Bounce a second request each frame traces rt_bounce.cs_6_5.hlsl against that frame's TLAS; bridge v8
// path traces it instead (rt_path.hlsli: a world radiance
// cache and the learned lighting for what the screen never saw), and a third request traces reflections and specular
// occlusion. All run as compute shaders (DXR 1.2's shader execution reordering made each pass slower in the harness at
// 640 x 360 to 4K, with and without clutter in view: the paths' hits need lookups, not divergent shading); Path Tracing
// SER (Experimental, v105) runs the paths as ray generation shaders that reorder at their first surface (rt_path_ser)
// for tests in the game's cities. With
// NVAPI's cluster operations (RTX Mega Geometry; set RENODX_TW_NO_CLUSTERS to turn them off) the units are built each
// frame as clusters (CLAS) into one cluster BLAS instead of the refitted BLAS; compact draws (bridge v16: a draw's
// vertices streamed once each, its cluster set sent once and kept in a pool) become clusters on the GPU
// (rt_compact.cs_6_5.hlsl). The partitioned TLAS (PTLAS: each frame
// only the instances that changed written; only with RENODX_TW_PTLAS: it stalled a city siege) takes every change as a
// whole instance write: NVAPI's update operation (a new BLAS for an instance) stalled the GPU 4-5 s a time after BLAS
// compactions (GPU harness, helper19-21). Two PTLASes in turn and the operations copied into device memory: ours, as first
// written (a test path), not NVAPI's ask: nvapi.h's PARTITIONED_TLAS_INDIRECT_DESC allows a build in place (the old PTLAS
// then invalid) and takes the operations in NON_PIXEL_SHADER_RESOURCE, which an upload buffer's GENERIC_READ holds. Its
// queue waits for the addon's fence value and signals the next.
namespace twattila::dlaa_helper {

using Microsoft::WRL::ComPtr;

class RtScene12 {
 public:
  // The device on the game's GPU, with DXR 1.1 (1.2 through the Agility SDK in `folder`\D3D12 when the driver has shader
  // model 6.9); false: unavailable (the status says why).
  // `details` (the addon's Log Details at the device's start): DRED, below. `any_gpu`: another vendor's GPU too (FSR or XeSS
  // chosen, which only Attila's list has: its own denoisers then; no other driver has run this yet, so Experimental);
  // not for good: a later set with it starts here. RENODX_TW_FAKE_VENDOR (tests): another vendor's.
  bool Initialize(IDXGIAdapter1* adapter, const std::filesystem::path& folder, bool details, bool any_gpu = false) {
    if (failed_) return false;
    if (device_) return true;
    folder_ = folder;
#if defined(RENODX_RECORDER)
    journal_on_ = GetEnvironmentVariableW(L"RENODX_TW_RECORD", nullptr, 0) != 0;
#endif
    DXGI_ADAPTER_DESC1 adapter_identity = {};
    adapter->GetDesc1(&adapter_identity);
    if (std::array<char, 16> fake = {}; GetEnvironmentVariableA("RENODX_TW_FAKE_VENDOR", fake.data(), static_cast<DWORD>(fake.size())) != 0) {
      adapter_identity.VendorId = std::strtoul(fake.data(), nullptr, 16);
    }
    if (adapter_identity.VendorId != 0x10DE && !any_gpu) {
      status_ = "Ray tracing needs an NVIDIA RTX GPU";
      return false;
    }
    // The Agility SDK through a device factory of its own (no developer mode; frame generation and Ray Reconstruction
    // keep Windows' runtime).
    std::string sdk = "Windows' D3D12";
    const std::filesystem::path agility = folder / L"D3D12";
    ComPtr<ID3D12SDKConfiguration1> configuration;
    ComPtr<ID3D12DeviceFactory> factory;
    // DRED (Device Removed Extended Data): after a GPU fault or hang (ATN 2026-10-28: frozen, then no ray tracing until
    // the game restarts), each command list's operations up to where the GPU stopped, the passes' markers, a page fault's
    // allocations (LostReport).
    const auto record_removal = [details](ID3D12DeviceRemovedExtendedDataSettings1* settings) {
      const D3D12_DRED_ENABLEMENT enablement = (details ? D3D12_DRED_ENABLEMENT_FORCED_ON : D3D12_DRED_ENABLEMENT_FORCED_OFF);
      settings->SetAutoBreadcrumbsEnablement(enablement);
      settings->SetPageFaultEnablement(enablement);
      settings->SetBreadcrumbContextEnablement(enablement);
    };
    if (std::filesystem::exists(agility / L"D3D12Core.dll")
        && SUCCEEDED(D3D12GetInterface(SDK_CONFIGURATION, IID_PPV_ARGS(configuration.GetAddressOf())))
        && SUCCEEDED(configuration->CreateDeviceFactory(AGILITY_SDK_VERSION, (agility.string() + "\\").c_str(),
                                                        IID_PPV_ARGS(factory.GetAddressOf())))) {
      factory->SetFlags(D3D12_DEVICE_FACTORY_FLAG_DISALLOW_STORING_NEW_DEVICE_AS_SINGLETON);
      if (ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> settings;
          SUCCEEDED(factory->GetConfigurationInterface(DRED_CONFIGURATION, IID_PPV_ARGS(settings.GetAddressOf())))) {
        record_removal(settings.Get());
      }
      // RENODX_TW_D3D12_DEBUG (tests; D3D12\d3d12SDKLayers.dll next to D3D12Core.dll): D3D12's debug layer, its messages
      // into the log after each request.
      if (GetEnvironmentVariableW(L"RENODX_TW_D3D12_DEBUG", nullptr, 0) != 0) {
        ComPtr<ID3D12Debug> debug;
        debug_result_ = factory->GetConfigurationInterface(DEBUG_CONFIGURATION, IID_PPV_ARGS(debug.GetAddressOf()));
        if (SUCCEEDED(debug_result_)) debug->EnableDebugLayer();
      }
      if (SUCCEEDED(factory->CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device_.GetAddressOf())))) sdk = "Agility SDK";
      if (device_) device_.As(&info_queue_);
    }
    if (ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> settings;
        !device_ && SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(settings.GetAddressOf())))) {
      record_removal(settings.Get());
    }
#if defined(RENODX_OMM_SDK)
    // Opacity micromaps for new leaves, baked by NVIDIA's OMM SDK (its omm-lib.dll next to the helper): its CPU baker by
    // default (the first GPU bake stopped ATN's GPU three times, 2026-10-28), its GPU baker with GPU Micromap Baking.
    if (HMODULE library = (omm_.library == nullptr) ? LoadLibraryW((folder / L"omm-lib.dll").c_str()) : nullptr; library != nullptr) {
      const auto load = [library](auto* function, const char* name) {
        *function = reinterpret_cast<std::remove_pointer_t<decltype(function)>>(GetProcAddress(library, name));
        return *function != nullptr;
      };
      decltype(&ommCreateBaker) create_baker = nullptr;
      ommBakerCreationDesc creation = ommBakerCreationDescDefault();
      creation.type = ommBakerType_CPU;
      const bool loaded = load(&create_baker, "ommCreateBaker") && load(&omm_.destroy_baker, "ommDestroyBaker")
                          && load(&omm_.create_texture, "ommCpuCreateTexture") && load(&omm_.destroy_texture, "ommCpuDestroyTexture")
                          && load(&omm_.bake, "ommCpuBake") && load(&omm_.result_desc, "ommCpuGetBakeResultDesc")
                          && load(&omm_.destroy_result, "ommCpuDestroyBakeResult");
      if (loaded && std::ranges::all_of(omm_.bakers, [&](ommBaker& baker) { return create_baker(&creation, &baker) == ommResult_SUCCESS; })) {
        omm_.library = library;
        // The GPU baker (GPU Micromap Baking; its pipelines made with the device: MakeGpuBaker).
        ommBakerCreationDesc gpu_creation = ommBakerCreationDescDefault();
        gpu_creation.type = ommBakerType_GPU;
        const ommGpuPipelineConfigDesc gpu_config = ommGpuPipelineConfigDescDefault();
        if (!load(&omm_.gpu_create_pipeline, "ommGpuCreatePipeline") || !load(&omm_.gpu_destroy_pipeline, "ommGpuDestroyPipeline")
            || !load(&omm_.gpu_pipeline_desc, "ommGpuGetPipelineDesc") || !load(&omm_.gpu_pre_dispatch, "ommGpuGetPreDispatchInfo")
            || !load(&omm_.gpu_dispatch, "ommGpuDispatch") || !load(&omm_.gpu_static_data, "ommGpuGetStaticResourceData")
            || create_baker(&gpu_creation, &omm_.gpu_baker) != ommResult_SUCCESS
            || omm_.gpu_create_pipeline(omm_.gpu_baker, &gpu_config, &omm_.gpu_pipeline) != ommResult_SUCCESS) {
          if (omm_.gpu_baker != nullptr) omm_.destroy_baker(omm_.gpu_baker);
          omm_.gpu_baker = nullptr;
          omm_.gpu_pipeline = nullptr;
        }
      } else {
        for (const ommBaker baker : omm_.bakers) {
          if (baker != nullptr) omm_.destroy_baker(baker);
        }
        FreeLibrary(library);
        omm_ = {};
      }
    }
    bake_micromaps_ = omm_.bakers[0] != nullptr;
#endif
    // Not for good: Windows refuses a process new devices for a while after GPU resets (ATN 2026-10-29: after four, "needs
    // DXR 1.1"); tried again at the next set.
    if (const HRESULT made = device_ ? S_OK : D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device_.GetAddressOf())); FAILED(made)) {
      status_ = std::format("Ray tracing: no D3D12 device on the game's GPU (0x{:08X})", static_cast<uint32_t>(made));
      return false;
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    if (FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)))
        || options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_1) {
      return Fail("Ray tracing needs a GPU and driver with DXR 1.1");
    }
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model = {.HighestShaderModel = D3D_SHADER_MODEL_6_9};
    // DXR 1.2's micromaps, reordering and SHaRC on NVIDIA's only for now: the path the harness's no_dxr12 modes run elsewhere.
    dxr12_ = adapter_identity.VendorId == 0x10DE && options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_2
             && GetEnvironmentVariableW(L"RENODX_TW_NO_DXR12", nullptr, 0) == 0
             && SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model)))
             && shader_model.HighestShaderModel >= D3D_SHADER_MODEL_6_9;
#if defined(RENODX_NVAPI_CLUSTERS)
    // RTX Mega Geometry: the driver's cluster operations, enabled for the pipelines made after this (they trace the
    // units' cluster BLAS).
    NvU32 cluster_caps = 0;
    clusters_ = GetEnvironmentVariableW(L"RENODX_TW_NO_CLUSTERS", nullptr, 0) == 0 && NvAPI_Initialize() == NVAPI_OK
                && NvAPI_D3D12_GetRaytracingCaps(device_.Get(), NVAPI_D3D12_RAYTRACING_CAPS_TYPE_CLUSTER_OPERATIONS, &cluster_caps,
                                                 sizeof(cluster_caps)) == NVAPI_OK
                && (cluster_caps & NVAPI_D3D12_RAYTRACING_CLUSTER_OPERATIONS_CAP_STANDARD) != 0;
    if (clusters_) {
      const NVAPI_D3D12_SET_CREATE_PIPELINE_STATE_OPTIONS_PARAMS options = {
          .version = NVAPI_D3D12_SET_CREATE_PIPELINE_STATE_OPTIONS_PARAMS_VER,
          .flags = NVAPI_D3D12_PIPELINE_CREATION_STATE_FLAGS_ENABLE_CLUSTER_SUPPORT,
      };
      clusters_ = NvAPI_D3D12_SetCreatePipelineStateOptions(device_.Get(), &options) == NVAPI_OK;
    }
#endif
    const D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
    if (GetEnvironmentVariableW(L"RENODX_TW_NO_GPU_TIMES", nullptr, 0) == 0) {
      const D3D12_QUERY_HEAP_DESC heap_desc = {.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP, .Count = static_cast<UINT>(SLOTS * STAMPS)};
      const D3D12_HEAP_PROPERTIES readback_heap = {.Type = D3D12_HEAP_TYPE_READBACK};
      const D3D12_RESOURCE_DESC readback_desc = {.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER, .Width = SLOTS * STAMPS * sizeof(uint64_t),
                                                 .Height = 1, .DepthOrArraySize = 1, .MipLevels = 1, .SampleDesc = {.Count = 1},
                                                 .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
      if (FAILED(device_->CreateQueryHeap(&heap_desc, IID_PPV_ARGS(timestamps_.GetAddressOf())))
          || FAILED(device_->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                     nullptr, IID_PPV_ARGS(timestamp_readback_.GetAddressOf())))) {
        timestamps_.Reset();
      }
    }
    if (const HRESULT made = device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(queue_.GetAddressOf())); FAILED(made)) {
      return Fail(std::format("Ray tracing: no D3D12 queue (0x{:08X})", static_cast<uint32_t>(made)));
    }
    if (const HRESULT made = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(own_fence_.GetAddressOf())); FAILED(made)) {
      return Fail(std::format("Ray tracing: no D3D12 fence (0x{:08X})", static_cast<uint32_t>(made)));
    }
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event_ == nullptr) return Fail(std::format("Ray tracing: no completion event (error {})", GetLastError()));
    for (Slot& slot : slots_) {
      slot.instances = Buffer(uint64_t{MAX_INSTANCES} * sizeof(D3D12_RAYTRACING_INSTANCE_DESC), D3D12_HEAP_TYPE_UPLOAD,
                              D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
      slot.table = Buffer(uint64_t{dlaa_bridge::RT_CAPTURES} * 32, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                          D3D12_RESOURCE_STATE_GENERIC_READ);
      slot.sizes = Buffer(uint64_t{COMPACTIONS} * sizeof(uint64_t), D3D12_HEAP_TYPE_DEFAULT,
                          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      slot.readback = Buffer(uint64_t{COMPACTIONS} * sizeof(uint64_t), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
                             D3D12_RESOURCE_STATE_COPY_DEST);
      slot.rows_readback = Buffer(uint64_t{ROWS_READBACK} * 64, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
      slot.jobs = Buffer(PLACE_TABLE_BYTES + uint64_t{PLACE_JOBS} * 32, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                         D3D12_RESOURCE_STATE_GENERIC_READ);
      slot.left_out = Buffer(LEFT_OUT_BYTES, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
      slot.left_out_readback = Buffer(LEFT_OUT_BYTES + sizeof(uint32_t), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
      if (!slot.instances || !slot.table || !slot.sizes || !slot.readback || !slot.rows_readback || !slot.jobs || !slot.left_out
          || !slot.left_out_readback) {
        return Fail("Ray tracing: no memory for its slots' buffers");
      }
#if defined(RENODX_NRD)
      slot.sun_readback = Buffer(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
      if (!slot.sun_readback || FAILED(slot.sun_readback->Map(0, nullptr, reinterpret_cast<void**>(&slot.sun_data)))) {
        return Fail("Ray tracing: no memory for its slots' buffers");
      }
#endif
      HRESULT made = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(slot.allocator.GetAddressOf()));
      if (SUCCEEDED(made)) {
        made = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot.allocator.Get(), nullptr, IID_PPV_ARGS(slot.list.GetAddressOf()));
      }
      if (SUCCEEDED(made)) made = slot.list->Close();
      if (SUCCEEDED(made)) made = slot.left_out_readback->Map(0, nullptr, reinterpret_cast<void**>(&slot.left_out_data));
      if (SUCCEEDED(made)) made = slot.instances->Map(0, nullptr, reinterpret_cast<void**>(&slot.instance_data));
      if (SUCCEEDED(made)) made = slot.table->Map(0, nullptr, reinterpret_cast<void**>(&slot.table_data));
      if (SUCCEEDED(made)) made = slot.jobs->Map(0, nullptr, reinterpret_cast<void**>(&slot.jobs_data));
      if (FAILED(made)) return Fail(std::format("Ray tracing: no D3D12 command lists (0x{:08X})", static_cast<uint32_t>(made)));
      slot.list->SetName(std::format(L"slot {}", &slot - slots_.data()).c_str());
    }
    // The vertex copy (BLAS inputs), the records and pools, the TLAS and its instances' room (PlacedRoom).
    vertex_buffer_ = Buffer(uint64_t{TRANSFER_VERTICES} * 16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_COPY_DEST);
    // The scene data pass's garbage count starts from these (none, no capture).
    left_out_start_ = Buffer(LEFT_OUT_BYTES, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    if (uint32_t* start = nullptr; left_out_start_ && SUCCEEDED(left_out_start_->Map(0, nullptr, reinterpret_cast<void**>(&start)))) {
      const std::array<uint32_t, LEFT_OUT_BYTES / 4> none = {0, 0, 0, UINT32_MAX};
      std::memcpy(start, none.data(), sizeof(none));
      left_out_start_->Unmap(0, nullptr);
    }
    // The smoke's TLAS: its one instance (none: the smoke stays out of the trace).
    const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS smoke_inputs = {
        .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
        .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
        .NumDescs = 1,
        .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
    };
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO smoke_info = {};
    device_->GetRaytracingAccelerationStructurePrebuildInfo(&smoke_inputs, &smoke_info);
    smoke_tlas_ = Buffer(smoke_info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    smoke_tlas_scratch_ = Buffer(smoke_info.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const auto pool = [&](uint64_t bytes) {
      return Buffer(bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    };
    // A request's alpha-tested corners (grass, swaying leaves): all on GPUs over 20 GB, half over 12 GB, else a quarter
    // (the addon reads it back: rt_dynamic_uvs).
    DXGI_ADAPTER_DESC1 adapter_desc = {};
    adapter->GetDesc1(&adapter_desc);
    // RENODX_TW_TEST_GPU_GB=<GB>: the tiers of a GPU of that size (GPU harness).
    std::array<wchar_t, 16> test_gigabytes = {};
    const size_t video_memory = (GetEnvironmentVariableW(L"RENODX_TW_TEST_GPU_GB", test_gigabytes.data(), static_cast<DWORD>(test_gigabytes.size())) != 0)
                                    ? (static_cast<size_t>(_wtoi(test_gigabytes.data())) << 30)
                                    : adapter_desc.DedicatedVideoMemory;
    dynamic_uvs_ = (video_memory > (size_t{20} << 30))   ? DYNAMIC_UVS
                   : (video_memory > (size_t{12} << 30)) ? DYNAMIC_UVS / 2
                                                         : DYNAMIC_UVS / 4;
    // The TLAS's instances grow up to D3D12's most over 20 GB, 8,388,608 over 12 GB, else 4,194,304 (powers of two: rt_place's table).
    placed_limit_ = (video_memory > (size_t{20} << 30))   ? D3D12_RAYTRACING_MAX_INSTANCES_PER_TOP_LEVEL_ACCELERATION_STRUCTURE
                    : (video_memory > (size_t{12} << 30)) ? (1u << 23)
                                                          : (1u << 22);
    uv_pool_ = STATIC_UVS + (SLOTS * dynamic_uvs_);
    records_ = pool(uint64_t{RECORD_COUNT} * 16);
    uvs_ = pool(uint64_t{uv_pool_} * 4);
    normals_ = pool(uint64_t{NORMAL_POOL} * 4);
    // Placed meshes (v14): their build rows by record.
    build_rows_ = Buffer(uint64_t{STATIC_RECORDS} * 48, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (PlacedRoom room; MakePlacedRoom(MAX_INSTANCES, &room)) {
      UsePlacedRoom(&room);
    }
    if (!vertex_buffer_ || !left_out_start_ || !tlas_ || !tlas_scratch_ || !records_ || !uvs_ || !normals_ || !build_rows_
        || !placed_instances_[0] || !placed_instances_[1] || !placed_state_ || !placed_scratch_) {
      return Fail("Ray tracing: D3D12 buffers failed");
    }
#if defined(RENODX_NVAPI_CLUSTERS)
    // Mega Geometry: the CLASes (placed by the driver, their addresses in clas_addresses_), the units' cluster BLAS (at
    // cluster_blases_, their addresses in cluster_destination_), the scratch and the arguments, all with room for a frame's
    // clusters (ClusterRoom); the sequential 8-bit indices; a slot's captures' arguments and records written by the CPU
    // (cluster_data), its compact draws' table and new sets (compact_data); the sets' pool. A cluster has at most
    // RT_CLUSTER_TRIANGLES triangles (a compact draw's) over 255 vertices (a capture's sequential ones) and takes ~6 KB of
    // CLAS memory at most: the room starts at CLUSTER_ROOM (ATN's battles drew 8,000-21,000 a frame) and grows while a
    // frame fills 3/4 of it, up to CLUSTER_TIERS by the GPU's memory: all over 28 GB, 3/4 over 20 GB, a quarter over 12 GB,
    // else an eighth (4x unit size battles filled 262,144 with city walls left out).
    if (clusters_) {
      cluster_limit_ = (video_memory > (size_t{28} << 30))   ? CLUSTER_TIERS[0]
                       : (video_memory > (size_t{20} << 30)) ? CLUSTER_TIERS[1]
                       : (video_memory > (size_t{12} << 30)) ? CLUSTER_TIERS[2]
                                                             : CLUSTER_TIERS[3];
      // Of the room a quarter built from triangles (the captures', the compact draws' whose set has no templates yet), the
      // rest instantiated.
      clas_inputs_ = {
          .flags = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_NONE,
          .type = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_CLAS_FROM_TRIANGLES,
          .mode = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_IMPLICIT_DESTINATIONS,
      };
      clas_inputs_.trianglesDesc = {
          .vertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
          .maxGeometryIndexValue = cluster_limit_ - 1,
          .maxUniqueGeometryCountPerArg = 1,
          .maxTriangleCountPerArg = dlaa_bridge::RT_CLUSTER_TRIANGLES,
          .maxVertexCountPerArg = CLUSTER_TRIANGLES * 3,
          .maxTotalTriangleCount = DYNAMIC_TRIANGLES,
          .maxTotalVertexCount = TRANSFER_VERTICES,  // each a texel of the request's
          .minPositionTruncateBitCount = 0,
      };
      // The cluster BLASes (CLUSTER_BLASES) built in one call.
      cluster_blas_inputs_ = {
          .maxArgCount = CLUSTER_BLASES,
          .flags = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_NONE,
          .type = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_BLAS_FROM_CLAS,
          .mode = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_EXPLICIT_DESTINATIONS,
      };
      // Templates (a set's opaque clusters' triangles, built once, explicit places in templates_) and their instantiation
      // each frame (the vertices in the vertex buffer) into the first batch of CLASes.
      template_inputs_ = {
          .maxArgCount = TEMPLATE_BUILDS,
          .flags = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_NONE,
          .type = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_CLUSTER_TEMPLATES_FROM_TRIANGLES,
          .mode = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_EXPLICIT_DESTINATIONS,
      };
      template_inputs_.trianglesDesc = {
          .vertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
          .maxGeometryIndexValue = cluster_limit_ - 1,
          .maxUniqueGeometryCountPerArg = 1,
          .maxTriangleCountPerArg = dlaa_bridge::RT_CLUSTER_TRIANGLES,
          .maxVertexCountPerArg = dlaa_bridge::RT_CLUSTER_VERTICES,
          .maxTotalTriangleCount = TEMPLATE_BUILDS * dlaa_bridge::RT_CLUSTER_TRIANGLES,
          .maxTotalVertexCount = TEMPLATE_BUILDS * dlaa_bridge::RT_CLUSTER_VERTICES,
          .minPositionTruncateBitCount = 0,
      };
      instantiate_inputs_ = {
          .flags = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_NONE,
          .type = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_INSTANTIATE_CLUSTER_TEMPLATES,
          .mode = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_IMPLICIT_DESTINATIONS,
      };
      instantiate_inputs_.trianglesDesc = {
          .vertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
          .maxGeometryIndexValue = cluster_limit_ - 1,
          .maxUniqueGeometryCountPerArg = 1,
          .maxTriangleCountPerArg = dlaa_bridge::RT_CLUSTER_TRIANGLES,
          .maxVertexCountPerArg = dlaa_bridge::RT_CLUSTER_VERTICES,
          .maxTotalTriangleCount = DYNAMIC_TRIANGLES,
          .maxTotalVertexCount = TRANSFER_VERTICES,
          .minPositionTruncateBitCount = 0,
      };
      // Grass micromaps: every cluster operation allows them (templates, instantiations and the BLAS from them read them);
      // without the driver's support for that, none.
      const auto allow_micromaps = [&](bool allowed) {
        const auto operation_flags = allowed ? NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_ALLOW_OMM
                                             : NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_NONE;
        for (auto* inputs : {&clas_inputs_, &cluster_blas_inputs_, &template_inputs_, &instantiate_inputs_}) {
          inputs->flags = operation_flags;
        }
        cluster_micromaps_ = allowed;
      };
      // A template's most bytes: its requirements as the only one.
      NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO template_info = {};
      const auto size_clusters = [&] {
        auto one_template = template_inputs_;
        one_template.maxArgCount = 1;
        ClusterRoom made;
        // GPUs over 20 GB start at 262,144 (~2.5 GB): ATN's battle start grew it four times (32 K to 320 K, 2026-11-01),
        // the swaying grass that did not fit left out meanwhile (its shadows flickered).
        const uint32_t room = (cluster_limit_ > CLUSTER_TIERS[2]) ? CLUSTER_TIERS[2] : std::min(cluster_limit_, CLUSTER_ROOM);
        if (!ClusterRequirements(one_template, &template_info) || !SizeClusterRoom(room, room / 4, &made) || !MakeClusterRoom(&made)) {
          return false;
        }
        UseClusterRoom(&made);
        return true;
      };
      allow_micromaps(dxr12_ && bake_micromaps_);
      clusters_ = size_clusters();
      if (!clusters_ && cluster_limit_ > CLUSTER_TIERS[2]) {
        cluster_limit_ = CLUSTER_TIERS[2];
        room_note_ = std::format("No memory: {} clusters at most | ", cluster_limit_);
        clusters_ = size_clusters();
      }
      if (!clusters_ && cluster_micromaps_) {
        allow_micromaps(false);
        clusters_ = size_clusters();
      }
      if (clusters_) {
        template_bytes_ = static_cast<uint32_t>(Align(template_info.resultDataMaxSizeInBytes, NVAPI_D3D12_RAYTRACING_CLUSTER_TEMPLATE_BYTE_ALIGNMENT));
        cluster_destination_ = ClusterBuffer(8, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cluster_indices_ = Buffer(256, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        set_pool_ = Buffer(uint64_t{SET_POOL_BYTES} + 16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        templates_ = ClusterBuffer(TEMPLATE_POOL_BYTES, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
        template_destinations_ = ClusterBuffer(uint64_t{TEMPLATE_BUILDS} * 8, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        template_geometry_ = Buffer(uint64_t{dlaa_bridge::RT_CLUSTER_TRIANGLES} * 4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
                                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        clusters_ = cluster_destination_ && cluster_indices_ && set_pool_ && templates_ && template_destinations_ && template_geometry_
                    && template_bytes_ != 0;
      }
      for (Slot& slot : slots_) {
        if (!clusters_) break;
        slot.clusters = Buffer(CLUSTER_UPLOAD_BYTES, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
        slot.compact = Buffer(COMPACT_UPLOAD_BYTES, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
        clusters_ = slot.clusters && SUCCEEDED(slot.clusters->Map(0, nullptr, reinterpret_cast<void**>(&slot.cluster_data))) && slot.compact
                    && SUCCEEDED(slot.compact->Map(0, nullptr, reinterpret_cast<void**>(&slot.compact_data)));
        if (clusters_) {
          for (uint32_t index = 0; index < 256; ++index) slot.cluster_data[CLUSTER_INDICES_OFFSET + index] = static_cast<uint8_t>(index);
        }
      }
      free_set_units_.free = {{0, SET_POOL_BYTES / 16}};
      if (clusters_) free_templates_.free = {{0, static_cast<uint32_t>(TEMPLATE_POOL_BYTES / template_bytes_)}};
    }
    // The partitioned TLAS: an instance per static record (its index) and one for the units, PTLAS_PARTITION_SIZE a
    // partition; a slot's write and update arguments, its two operations and their count. Only with RENODX_TW_PTLAS:
    // a city siege in Attila stalled each frame by ~0.3 s with it, the TLAS built whole costs 0.10-0.16 ms (ATN's log
    // 2026-10-29: all a PTLAS could save), and it has no placed meshes (v14: rt_place writes the TLAS's descs only).
    NvU32 ptlas_caps = 0;
    ptlas_ = nullptr;
    if (GetEnvironmentVariableW(L"RENODX_TW_PTLAS", nullptr, 0) != 0 && NvAPI_Initialize() == NVAPI_OK
        && NvAPI_D3D12_GetRaytracingCaps(device_.Get(), NVAPI_D3D12_RAYTRACING_CAPS_TYPE_PARTITIONED_TLAS, &ptlas_caps, sizeof(ptlas_caps)) == NVAPI_OK
        && (ptlas_caps & NVAPI_D3D12_RAYTRACING_PARTITIONED_TLAS_CAP_STANDARD) != 0) {
      ptlas_inputs_ = {
          .flags = NVAPI_D3D12_RAYTRACING_PARTITIONED_TLAS_FLAG_FAST_TRACE,
          .instanceCount = PTLAS_INSTANCES,
          .maxInstancePerPartitionCount = PTLAS_PARTITION_SIZE,
          .partitionCount = (PTLAS_INSTANCES + PTLAS_PARTITION_SIZE - 1) / PTLAS_PARTITION_SIZE,
          .maxInstanceInGlobalPartitionCount = 0,
      };
      NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_PREBUILD_INFO info = {};
      const NVAPI_GET_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_PREBUILD_INFO_PARAMS params = {
          .version = NVAPI_GET_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_PREBUILD_INFO_PARAMS_VER, .pInput = &ptlas_inputs_, .pInfo = &info};
      bool made = NvAPI_D3D12_GetRaytracingPartitionedTlasIndirectPrebuildInfo(device_.Get(), &params) == NVAPI_OK;
      // Two partitioned TLASes in turn (each build reads the last one, never in place) and the operations in device memory
      // (NVAPI: indirectOps / indirectOpCount in NON_PIXEL_SHADER_RESOURCE), copied from the slot's upload buffer.
      if (made) {
        for (auto& buffer : ptlas_buffers_) {
          buffer = Buffer(info.resultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                          D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
          made = made && buffer;
        }
        ptlas_scratch_ = Buffer(std::max<uint64_t>(info.scratchDataSizeInBytes, 256), D3D12_HEAP_TYPE_DEFAULT,
                                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        made = made && ptlas_scratch_;
      }
      for (Slot& slot : slots_) {
        if (!made) break;
        slot.ptlas = Buffer(PTLAS_UPLOAD_BYTES, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
        slot.ptlas_device = Buffer(PTLAS_UPLOAD_BYTES, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        made = slot.ptlas && slot.ptlas_device && SUCCEEDED(slot.ptlas->Map(0, nullptr, reinterpret_cast<void**>(&slot.ptlas_data)));
      }
      if (made) {
        ptlas_current_ = 0;
        ptlas_ = ptlas_buffers_[0].Get();
        ptlas_written_.assign(PTLAS_INSTANCES, {});
        ptlas_wanted_.assign(PTLAS_INSTANCES, {});
      }
    }
#endif
    free_triangles_.free = {{0, STATIC_TRIANGLES}};
    free_instances_.free = {{STATIC_RECORDS, MAX_INSTANCES - STATIC_RECORDS}};
    free_uvs_.free = {{0, STATIC_UVS}};
    for (uint32_t record = STATIC_RECORDS; record > 0; --record) {
      free_records_.push_back(record - 1);
    }
    // The passes: t0 the TLAS (root SRV); the table (Descriptor); b0 the settings, b1 a dispatch's (root constants); t8
    // the capture table, u6 / u7 / u8 the micromaps' buffers (root descriptors); s0 linear wrap.
    const std::array<D3D12_DESCRIPTOR_RANGE, 6> ranges = {{
        {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV, .NumDescriptors = SRV_COUNT, .BaseShaderRegister = 1, .OffsetInDescriptorsFromTableStart = 0},
        {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV, .NumDescriptors = LIGHT - SRV_COUNT, .BaseShaderRegister = 0,
         .OffsetInDescriptorsFromTableStart = SRV_COUNT},
        {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV, .NumDescriptors = 1, .BaseShaderRegister = 9, .OffsetInDescriptorsFromTableStart = LIGHT},
        {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV, .NumDescriptors = 1, .BaseShaderRegister = 9, .OffsetInDescriptorsFromTableStart = BOUNCE},
        // Paths (v8): t10-t14, u10-u21; the local lights' mask (v9) u22; ReSTIR GI's reservoirs u23-u26; the fog (v11) u27.
        {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV, .NumDescriptors = CACHE_KEYS - PREVIOUS_COLOR, .BaseShaderRegister = 10,
         .OffsetInDescriptorsFromTableStart = PREVIOUS_COLOR},
        {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV, .NumDescriptors = DESCRIPTOR_COUNT - CACHE_KEYS, .BaseShaderRegister = 10,
         .OffsetInDescriptorsFromTableStart = CACHE_KEYS},
    }};
    const std::array<D3D12_ROOT_PARAMETER, 13> parameters = {{
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV, .Descriptor = {.ShaderRegister = 0}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
         .DescriptorTable = {.NumDescriptorRanges = static_cast<UINT>(ranges.size()), .pDescriptorRanges = ranges.data()}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, .Constants = {.ShaderRegister = 0, .Num32BitValues = 12}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, .Constants = {.ShaderRegister = 1, .Num32BitValues = 8}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV, .Descriptor = {.ShaderRegister = 8}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV, .Descriptor = {.ShaderRegister = 6}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV, .Descriptor = {.ShaderRegister = 7}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV, .Descriptor = {.ShaderRegister = 8}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, .Constants = {.ShaderRegister = 2, .Num32BitValues = 20}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV, .Descriptor = {.ShaderRegister = 3}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV, .Descriptor = {.ShaderRegister = 4}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV, .Descriptor = {.ShaderRegister = 15}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV, .Descriptor = {.ShaderRegister = 0, .RegisterSpace = 1}},
    }};
    const D3D12_STATIC_SAMPLER_DESC sampler = {
        .Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        .AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        .AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        .AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        .MaxLOD = D3D12_FLOAT32_MAX,
        .ShaderRegister = 0,
    };
    const D3D12_ROOT_SIGNATURE_DESC root_desc = {
        .NumParameters = static_cast<UINT>(parameters.size()),
        .pParameters = parameters.data(),
        .NumStaticSamplers = 1,
        .pStaticSamplers = &sampler,
    };
    ComPtr<ID3DBlob> serialized;
    const D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, .NumDescriptors = DESCRIPTOR_COUNT * TABLES,
                                                  .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    if (FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, serialized.GetAddressOf(), nullptr))
        || FAILED(device_->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                               IID_PPV_ARGS(root_signature_.GetAddressOf())))) {
      return Fail("Ray tracing: the root signature failed");
    }
#if defined(RENODX_RECORDER)
    // The resource-use log's registers (UseTable, UseRootSlot; the table parameter's ranges pointer is not used).
    use_ranges_ = ranges;
    use_parameters_ = parameters;
#endif
    const auto pipeline = [&](const void* code, size_t bytes, ComPtr<ID3D12PipelineState>* state) {
      const D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {.pRootSignature = root_signature_.Get(), .CS = {.pShaderBytecode = code, .BytecodeLength = bytes}};
      const bool made = SUCCEEDED(device_->CreateComputePipelineState(&desc, IID_PPV_ARGS(state->GetAddressOf())));
#if defined(RENODX_RECORDER)
      if (made) {
        KeepCode(state->Get(), code, bytes);
      }
#endif
      return made;
    };
    // DXR 1.2: the ray queries that use the micromaps; else (or when they fail) DXR 1.1.
    dxr12_ = dxr12_ && pipeline(RT_TRACE_OMM, sizeof(RT_TRACE_OMM), &trace_pipeline_)
             && pipeline(RT_BOUNCE_OMM, sizeof(RT_BOUNCE_OMM), &bounce_pipeline_) && pipeline(RT_PATH_OMM, sizeof(RT_PATH_OMM), &path_pipeline_)
             && pipeline(RT_REFLECT_OMM, sizeof(RT_REFLECT_OMM), &reflect_pipeline_) && pipeline(RT_REUSE_OMM, sizeof(RT_REUSE_OMM), &reuse_pipeline_)
             && pipeline(RT_REFLECT_RESERVOIR_OMM, sizeof(RT_REFLECT_RESERVOIR_OMM), &reflect_reservoir_pipeline_)
             && pipeline(RT_FOG_OMM, sizeof(RT_FOG_OMM), &fog_pipeline_) && pipeline(RT_LIGHTS_OMM, sizeof(RT_LIGHTS_OMM), &lights_pipeline_);
    if (!dxr12_) {
      trace_pipeline_.Reset();
      bounce_pipeline_.Reset();
      path_pipeline_.Reset();
      reflect_pipeline_.Reset();
      reuse_pipeline_.Reset();
      reflect_reservoir_pipeline_.Reset();
      fog_pipeline_.Reset();
      lights_pipeline_.Reset();
    }
    if ((!trace_pipeline_ && !pipeline(RT_TRACE, sizeof(RT_TRACE), &trace_pipeline_))
        || (!bounce_pipeline_ && !pipeline(RT_BOUNCE, sizeof(RT_BOUNCE), &bounce_pipeline_)) || !pipeline(RT_SOFT, sizeof(RT_SOFT), &soft_pipeline_)
        || !pipeline(RT_SCENE_DATA, sizeof(RT_SCENE_DATA), &scene_data_pipeline_) || !pipeline(RT_PLACE, sizeof(RT_PLACE), &place_pipeline_)
        || (!path_pipeline_ && !pipeline(RT_PATH, sizeof(RT_PATH), &path_pipeline_))
        || (!reflect_pipeline_ && !pipeline(RT_REFLECT, sizeof(RT_REFLECT), &reflect_pipeline_))
        || (!reuse_pipeline_ && !pipeline(RT_REUSE, sizeof(RT_REUSE), &reuse_pipeline_))
        || (!reflect_reservoir_pipeline_ && !pipeline(RT_REFLECT_RESERVOIR, sizeof(RT_REFLECT_RESERVOIR), &reflect_reservoir_pipeline_))
        || (!fog_pipeline_ && !pipeline(RT_FOG, sizeof(RT_FOG), &fog_pipeline_))
        || (!lights_pipeline_ && !pipeline(RT_LIGHTS, sizeof(RT_LIGHTS), &lights_pipeline_))
        || !pipeline(RT_CACHE_FILL, sizeof(RT_CACHE_FILL), &cache_fill_pipeline_)
        || !pipeline(RT_CACHE_RESOLVE, sizeof(RT_CACHE_RESOLVE), &cache_resolve_pipeline_)
        || !pipeline(RT_REFLECT_FILTER, sizeof(RT_REFLECT_FILTER), &reflect_filter_pipeline_)
        || FAILED(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(descriptors_.GetAddressOf())))) {
      return Fail("Ray tracing: the trace shaders failed (needs shader model 6.5)");
    }
    // Torch and Fire Light in the Light Bounce's builds (TORCHES_IN_BOUNCE; none: its rows do nothing).
    if (!(dxr12_ ? pipeline(RT_REUSE_LIGHTS_OMM, sizeof(RT_REUSE_LIGHTS_OMM), &reuse_lights_pipeline_)
                       && pipeline(RT_LIGHTS_BOUNCE_OMM, sizeof(RT_LIGHTS_BOUNCE_OMM), &lights_bounce_pipeline_)
                 : pipeline(RT_REUSE_LIGHTS, sizeof(RT_REUSE_LIGHTS), &reuse_lights_pipeline_)
                       && pipeline(RT_LIGHTS_BOUNCE, sizeof(RT_LIGHTS_BOUNCE), &lights_bounce_pipeline_))) {
      reuse_lights_pipeline_.Reset();
      lights_bounce_pipeline_.Reset();
    }
    // Lean Reuse's and Near Reuse's builds (REUSE_LEAN, REUSE_NEAR; none: its switch does nothing).
    if (!(dxr12_ ? pipeline(RT_REUSE_LEAN_OMM, sizeof(RT_REUSE_LEAN_OMM), &reuse_lean_pipeline_)
                 : pipeline(RT_REUSE_LEAN, sizeof(RT_REUSE_LEAN), &reuse_lean_pipeline_))) {
      reuse_lean_pipeline_.Reset();
    }
    if (!(dxr12_ ? pipeline(RT_REUSE_NEAR_OMM, sizeof(RT_REUSE_NEAR_OMM), &reuse_near_pipeline_)
                 : pipeline(RT_REUSE_NEAR, sizeof(RT_REUSE_NEAR), &reuse_near_pipeline_))) {
      reuse_near_pipeline_.Reset();
    }
#if defined(RENODX_REUSE_COMPATIBLE)
    // Matched ReSTIR Neighbours' build and its Balanced ReSTIR Light twin, both or neither (none: the switch does nothing).
    bool compatible_made = false;
    if (dxr12_) {
      compatible_made = pipeline(RT_REUSE_COMPATIBLE_OMM, sizeof(RT_REUSE_COMPATIBLE_OMM), &reuse_compatible_pipeline_);
      if (compatible_made) {
        compatible_made = pipeline(RT_REUSE_COMPATIBLE_BALANCED_OMM, sizeof(RT_REUSE_COMPATIBLE_BALANCED_OMM),
                                   &reuse_compatible_balanced_pipeline_);
      }
    } else {
      compatible_made = pipeline(RT_REUSE_COMPATIBLE, sizeof(RT_REUSE_COMPATIBLE), &reuse_compatible_pipeline_);
      if (compatible_made) {
        compatible_made = pipeline(RT_REUSE_COMPATIBLE_BALANCED, sizeof(RT_REUSE_COMPATIBLE_BALANCED),
                                   &reuse_compatible_balanced_pipeline_);
      }
    }
    if (!compatible_made) {
      reuse_compatible_pipeline_.Reset();
      reuse_compatible_balanced_pipeline_.Reset();
    } else {
      // Its Balanced ReSTIR Light Raytraced twin (none: Raytraced runs the Basic one there).
      const std::span<const unsigned char> code = (dxr12_ ? std::span<const unsigned char>(RT_REUSE_COMPATIBLE_BALANCED_RAYTRACED_OMM)
                                                          : std::span<const unsigned char>(RT_REUSE_COMPATIBLE_BALANCED_RAYTRACED));
      if (!pipeline(code.data(), code.size(), &reuse_compatible_balanced_raytraced_pipeline_)) {
        reuse_compatible_balanced_raytraced_pipeline_.Reset();
      }
    }
#endif
    // Balanced ReSTIR Light's twins, Basic's and Raytraced's, each set all or none (none: the row does nothing, or its
    // Raytraced runs Basic; the ready line says so).
    const std::array<BalancedTwin, 4> twins = BalancedTwins();
    for (const bool raytraced : {false, true}) {
      std::array<ComPtr<ID3D12PipelineState>, 4>& made_pipelines = (raytraced ? reuse_balanced_raytraced_pipelines_ : reuse_balanced_pipelines_);
      bool balanced_made = true;
      for (size_t kind = 0; kind < twins.size(); ++kind) {
        const BalancedTwin& twin = twins[kind];
        const std::span<const unsigned char> code = (raytraced ? (dxr12_ ? twin.code_raytraced_omm : twin.code_raytraced)
                                                               : (dxr12_ ? twin.code_omm : twin.code));
        balanced_made = balanced_made && pipeline(code.data(), code.size(), &made_pipelines[kind]);
      }
      if (!balanced_made) {
        for (ComPtr<ID3D12PipelineState>& state : made_pipelines) {
          state.Reset();
        }
      }
    }
#if defined(RENODX_REUSE_TESTS)
    // ReSTIR GI's test switches' builds (REUSE_TESTS: the paths', the reuse's; none: those switches do nothing).
    if (!(dxr12_ ? pipeline(RT_PATH_TESTS_OMM, sizeof(RT_PATH_TESTS_OMM), &path_tests_pipeline_)
                       && pipeline(RT_REUSE_TESTS_OMM, sizeof(RT_REUSE_TESTS_OMM), &reuse_tests_pipeline_)
                 : pipeline(RT_PATH_TESTS, sizeof(RT_PATH_TESTS), &path_tests_pipeline_)
                       && pipeline(RT_REUSE_TESTS, sizeof(RT_REUSE_TESTS), &reuse_tests_pipeline_))) {
      path_tests_pipeline_.Reset();
      reuse_tests_pipeline_.Reset();
    }
#endif
#if defined(RENODX_RTXDI)
    // ReSTIR GI's boiling filter (REUSE_BOIL: no ray tracing, one build; none: its switch does nothing).
    if (!pipeline(RT_REUSE_BOIL, sizeof(RT_REUSE_BOIL), &reuse_boil_pipeline_)) {
      reuse_boil_pipeline_.Reset();
    }
#endif
#if defined(RENODX_REGIR)
    // Torch Light in Path-Traced GI's ReGIR build (no ray tracing; none: the paths' torch light stays LocalLights') and
    // rt_lights' twins with its T1, both or none (none: the paths trace every hit's torch light).
    if (!pipeline(RT_REGIR_PRESAMPLE, sizeof(RT_REGIR_PRESAMPLE), &regir_pipeline_)) {
      regir_pipeline_.Reset();
    }
    if (!(dxr12_ ? pipeline(RT_LIGHTS_TORCH_OMM, sizeof(RT_LIGHTS_TORCH_OMM), &lights_torch_pipelines_[0])
                       && pipeline(RT_LIGHTS_BOUNCE_TORCH_OMM, sizeof(RT_LIGHTS_BOUNCE_TORCH_OMM), &lights_torch_pipelines_[1])
                 : pipeline(RT_LIGHTS_TORCH, sizeof(RT_LIGHTS_TORCH), &lights_torch_pipelines_[0])
                       && pipeline(RT_LIGHTS_BOUNCE_TORCH, sizeof(RT_LIGHTS_BOUNCE_TORCH), &lights_torch_pipelines_[1]))) {
      for (ComPtr<ID3D12PipelineState>& state : lights_torch_pipelines_) {
        state.Reset();
      }
    }
#if defined(RENODX_SHARC)
    // rt_sharc_update's twin with T2 (none: the update keeps LocalLights' torch light while the paths take T2).
    if (!(dxr12_ && pipeline(RT_SHARC_UPDATE_REGIR, sizeof(RT_SHARC_UPDATE_REGIR), &sharc_update_regir_pipeline_))) {
      sharc_update_regir_pipeline_.Reset();
    }
#endif
#endif
#if defined(RENODX_RTXDI_GI)
    // The RTXDI GI contest's (REUSE_RTXDI_PACK: no ray tracing, one build; REUSE_RTXDI: the reuse's).
    if (!(pipeline(RT_REUSE_RTXDI_PACK, sizeof(RT_REUSE_RTXDI_PACK), &reuse_rtxdi_pack_pipeline_)
          && (dxr12_ ? pipeline(RT_REUSE_RTXDI_OMM, sizeof(RT_REUSE_RTXDI_OMM), &reuse_rtxdi_pipeline_)
                     : pipeline(RT_REUSE_RTXDI, sizeof(RT_REUSE_RTXDI), &reuse_rtxdi_pipeline_)))) {
      reuse_rtxdi_pack_pipeline_.Reset();
      reuse_rtxdi_pipeline_.Reset();
    }
#endif
    // The reflections' build for one sample a pixel (REFLECT_ONE_SAMPLE) and the paths' for one ray a texel
    // (PATH_ONE_SAMPLE; none: the general build).
    if (!(dxr12_ ? pipeline(RT_REFLECT_ONE_OMM, sizeof(RT_REFLECT_ONE_OMM), &reflect_one_pipeline_)
                 : pipeline(RT_REFLECT_ONE, sizeof(RT_REFLECT_ONE), &reflect_one_pipeline_))) {
      reflect_one_pipeline_.Reset();
    }
    if (!(dxr12_ ? pipeline(RT_PATH_ONE_OMM, sizeof(RT_PATH_ONE_OMM), &path_one_pipeline_)
                 : pipeline(RT_PATH_ONE, sizeof(RT_PATH_ONE), &path_one_pipeline_))) {
      path_one_pipeline_.Reset();
    }
    clusters_ = clusters_ && pipeline(RT_COMPACT, sizeof(RT_COMPACT), &compact_pipeline_);
    // DXR 1.2: the paths as ray generation shaders for Path Tracing SER (none: their compute shaders), the general build
    // and the one of one ray a texel, each its state object and its record (the general at 0, the other at
    // PATH_SER_RECORD).
    if (dxr12_) {
      const D3D12_EXPORT_DESC ray_generation = {.Name = L"PathTraceRayGen"};
      const D3D12_RAYTRACING_SHADER_CONFIG shader_config = {.MaxPayloadSizeInBytes = 4, .MaxAttributeSizeInBytes = 8};
      const D3D12_RAYTRACING_PIPELINE_CONFIG1 pipeline_config = {.MaxTraceRecursionDepth = 1,
                                                                 .Flags = D3D12_RAYTRACING_PIPELINE_FLAG_ALLOW_OPACITY_MICROMAPS};
      const D3D12_GLOBAL_ROOT_SIGNATURE global = {.pGlobalRootSignature = root_signature_.Get()};
      uint8_t* table = nullptr;
      path_ser_table_ = Buffer(PATH_SER_RECORD * 2, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
      if (path_ser_table_ && FAILED(path_ser_table_->Map(0, nullptr, reinterpret_cast<void**>(&table)))) table = nullptr;
      const auto make_ser = [&](const void* code, size_t bytes, ComPtr<ID3D12StateObject>* state, uint64_t record) {
        const D3D12_DXIL_LIBRARY_DESC library = {.DXILLibrary = {.pShaderBytecode = code, .BytecodeLength = bytes},
                                                 .NumExports = 1,
                                                 .pExports = const_cast<D3D12_EXPORT_DESC*>(&ray_generation)};
        const std::array<D3D12_STATE_SUBOBJECT, 4> subobjects = {{
            {.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, .pDesc = &library},
            {.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, .pDesc = &shader_config},
            {.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1, .pDesc = &pipeline_config},
            {.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, .pDesc = &global},
        }};
        const D3D12_STATE_OBJECT_DESC state_desc = {.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE,
                                                    .NumSubobjects = static_cast<UINT>(subobjects.size()),
                                                    .pSubobjects = subobjects.data()};
        ComPtr<ID3D12StateObjectProperties> properties;
        if (table != nullptr && SUCCEEDED(device_->CreateStateObject(&state_desc, IID_PPV_ARGS(state->GetAddressOf())))
            && SUCCEEDED(state->As(&properties)) && properties->GetShaderIdentifier(L"PathTraceRayGen") != nullptr) {
          std::memcpy(table + record, properties->GetShaderIdentifier(L"PathTraceRayGen"), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        } else {
          state->Reset();
        }
      };
      make_ser(RT_PATH_SER, sizeof(RT_PATH_SER), std::addressof(path_ser_), 0);
      make_ser(RT_PATH_SER_ONE, sizeof(RT_PATH_SER_ONE), std::addressof(path_ser_one_), PATH_SER_RECORD);
      if (table != nullptr) path_ser_table_->Unmap(0, nullptr);
      if (!path_ser_) {
        path_ser_one_.Reset();
        path_ser_table_.Reset();
      }
    }
    if (clusters_ && !pipeline(RT_SWAY, sizeof(RT_SWAY), &sway_pipeline_)) sway_pipeline_.Reset();
#if defined(RENODX_PATH_PROBE)
    if (!(dxr12_ ? pipeline(RT_PATH_PROBE_OMM, sizeof(RT_PATH_PROBE_OMM), &path_probe_pipeline_)
                 : pipeline(RT_PATH_PROBE, sizeof(RT_PATH_PROBE), &path_probe_pipeline_))) {
      path_probe_pipeline_.Reset();
    }
#endif
#if defined(RENODX_REFERENCE)
    // RENODX_TW_REFERENCE=<paths | reflect>[,<samples a texel a frame>[,<samples a texel in all>]] (tests): the reference's run
    // (Reference); unset, nothing is made.
    if (std::array<char, 64> asked = {}; GetEnvironmentVariableA("RENODX_TW_REFERENCE", asked.data(), static_cast<DWORD>(asked.size())) != 0) {
      const auto number = [](const char* name, double fallback) {
        std::array<char, 32> value = {};
        return (GetEnvironmentVariableA(name, value.data(), static_cast<DWORD>(value.size())) != 0) ? std::atof(value.data()) : fallback;
      };
      const std::string_view request = asked.data();
      reference_.effect = request.starts_with("reflect") ? ReferenceEffect::REFLECT : ReferenceEffect::PATHS;
      if (const size_t comma = request.find(','); comma != std::string_view::npos) {
        char* rest = nullptr;
        reference_.slice_samples = std::max<uint32_t>(static_cast<uint32_t>(std::strtoul(asked.data() + comma + 1, &rest, 10)), 1u);
        if (*rest == ',') reference_.total_samples = std::max<uint32_t>(static_cast<uint32_t>(std::strtoul(rest + 1, nullptr, 10)), 1u);
      }
      reference_.settle = static_cast<uint32_t>(number("RENODX_TW_REF_SETTLE", 150));
      reference_.frames = std::max(static_cast<uint32_t>(number("RENODX_TW_REF_FRAMES", 384)) / REF_BATCH * REF_BATCH, REF_BATCH * 2);
      reference_.groups = std::clamp(static_cast<uint32_t>(number("RENODX_TW_REF_GROUPS", 8)), 2u, 64u);
      reference_.seed = static_cast<uint32_t>(number("RENODX_TW_REF_SEED", 0));
      reference_.scale = static_cast<float>(number("RENODX_TW_REF_SCALE", 1));
      reference_.direct = number("RENODX_TW_REF_DIRECT", 0) != 0;
      std::array<char, 32> variant = {};
      GetEnvironmentVariableA("RENODX_TW_REF_VARIANT", variant.data(), static_cast<DWORD>(variant.size()));
      const auto variant_found = std::ranges::find(REF_VARIANTS, std::string_view(variant.data()));
      reference_.variant = (variant_found != REF_VARIANTS.end()) ? static_cast<uint32_t>(variant_found - REF_VARIANTS.begin()) : 0u;
      // A value longer than its buffer (GetEnvironmentVariableA returns the size it needs) reads as unknown: no run.
      const bool break_fits = GetEnvironmentVariableA("RENODX_TW_REF_BREAK", reference_.code_break_asked.data(), static_cast<DWORD>(reference_.code_break_asked.size()))
                              < reference_.code_break_asked.size();
      reference_.code_break = break_fits ? static_cast<uint32_t>(std::ranges::find(REF_BREAKS, std::string_view(reference_.code_break_asked.data())) - REF_BREAKS.begin())
                                         : static_cast<uint32_t>(REF_BREAKS.size());
      const bool inside_fits = GetEnvironmentVariableA("RENODX_TW_REF_INSIDE", reference_.inside_asked.data(), static_cast<DWORD>(reference_.inside_asked.size()))
                               < reference_.inside_asked.size();
      reference_.inside_mode = inside_fits ? static_cast<uint32_t>(std::ranges::find(REF_INSIDE_MODES, std::string_view(reference_.inside_asked.data())) - REF_INSIDE_MODES.begin())
                                           : static_cast<uint32_t>(REF_INSIDE_MODES.size());
      std::array<char, 64> furnace = {};
      if (GetEnvironmentVariableA("RENODX_TW_REF_FURNACE", furnace.data(), static_cast<DWORD>(furnace.size())) != 0) {
        char* rest = nullptr;
        reference_.furnace[0] = std::strtof(furnace.data(), &rest);
        if (*rest == ',') reference_.furnace[1] = std::strtof(rest + 1, nullptr);
      }
      std::array<wchar_t, MAX_PATH> dump = {};
      reference_.folder = (GetEnvironmentVariableW(L"RENODX_TW_REF_DUMP", dump.data(), static_cast<DWORD>(dump.size())) != 0)
                              ? std::filesystem::path(dump.data())
                              : (folder / L"tw_reference");
      const std::array<std::pair<std::span<const uint8_t>, ComPtr<ID3D12PipelineState>*>, 7> passes = {{
          {RT_REFERENCE_ACCUMULATE, &reference_.accumulate},
          {RT_REFERENCE_CENSUS, &reference_.census},
          {RT_REFERENCE_DIGEST, &reference_.digest},
          {RT_REFERENCE_STATE, &reference_.state},
          {RT_REFERENCE_GROUP, &reference_.group},
          {RT_REFERENCE_PATHS, &reference_.paths},
          {RT_REFERENCE_REFLECT, &reference_.reflect},
      }};
      const bool made = std::ranges::all_of(passes, [&](const auto& pass) { return pipeline(pass.first.data(), pass.first.size(), pass.second); });
      reference_.phase = made ? ReferencePhase::SETTLE : ReferencePhase::OFF;
      reference_.note = std::format("\n[TW reference] {} {} {} samples ({} a frame), settle {}, window {}, {} groups, seed {}{}", made ? "on:" : "its shaders failed:",
                                  (reference_.effect == ReferenceEffect::REFLECT) ? "reflect" : "paths", REF_VARIANTS[reference_.variant],
                                  reference_.total_samples, reference_.slice_samples, reference_.settle, reference_.frames, reference_.groups, reference_.seed,
                                  (reference_.scale != 1.f) ? std::format(", x{:.3f}", reference_.scale) : std::string());
    }
#endif
#if defined(RENODX_SHARC)
    // SHaRC Radiance Cache (shader model 6.9 here: 64-bit atomics, 16-bit types): its pipelines, or none (the status says
    // which failed).
    if (dxr12_) {
      const std::array<std::tuple<const void*, size_t, ComPtr<ID3D12PipelineState>*, const char*>, 4> sharc_pipelines = {{
          {RT_PATH_SHARC, sizeof(RT_PATH_SHARC), &sharc_path_pipeline_, "paths"},
          {RT_PATH_SHARC_ONE, sizeof(RT_PATH_SHARC_ONE), &sharc_path_one_pipeline_, "paths of one ray"},
          {RT_SHARC_UPDATE, sizeof(RT_SHARC_UPDATE), &sharc_update_pipeline_, "update"},
          {RT_SHARC_RESOLVE, sizeof(RT_SHARC_RESOLVE), &sharc_resolve_pipeline_, "resolve"},
      }};
      for (const auto& [code, bytes, state, name] : sharc_pipelines) {
        if (sharc_failed_.empty() && !pipeline(code, bytes, state)) sharc_failed_ = name;
      }
      if (!sharc_failed_.empty()) {
        for (const auto& sharc_pipeline : sharc_pipelines) std::get<2>(sharc_pipeline)->Reset();
      }
    }
#endif
    std::string gpu_baker = "no micromaps";
#if defined(RENODX_OMM_SDK)
    if (dxr12_ && bake_micromaps_) gpu_baker = pipeline(RT_OMM_UVS, sizeof(RT_OMM_UVS), &omm_gpu_.clamp) ? std::string() : "rt_omm_uvs";
    gpu_micromaps_ = gpu_baker.empty() && MakeGpuBaker(&gpu_baker);
#endif
    for (Slot& slot : slots_) {
      slot.lights = Buffer(LIGHT_CONSTANT_BYTES, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
      slot.paths = Buffer(256, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
      if (!slot.lights || FAILED(slot.lights->Map(0, nullptr, reinterpret_cast<void**>(&slot.light_data))) || !slot.paths
          || FAILED(slot.paths->Map(0, nullptr, reinterpret_cast<void**>(&slot.paths_data)))) {
        return Fail("Ray tracing: no D3D12 constant buffers");
      }
    }
    descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const auto buffer_view = [](DXGI_FORMAT format, UINT elements) {
      return D3D12_SHADER_RESOURCE_VIEW_DESC{.Format = format, .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                                             .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                             .Buffer = {.NumElements = elements}};
    };
    const auto buffer_uav = [](DXGI_FORMAT format, UINT elements) {
      return D3D12_UNORDERED_ACCESS_VIEW_DESC{.Format = format, .ViewDimension = D3D12_UAV_DIMENSION_BUFFER, .Buffer = {.NumElements = elements}};
    };
    const auto records_view = buffer_view(DXGI_FORMAT_R32G32B32A32_UINT, RECORD_COUNT);
    const auto uvs_view = buffer_view(DXGI_FORMAT_R32_UINT, uv_pool_);
    const auto normals_view = buffer_view(DXGI_FORMAT_R32_UINT, NORMAL_POOL);
    const auto vertices_view = buffer_view(DXGI_FORMAT_R32G32B32A32_FLOAT, TRANSFER_VERTICES);
    device_->CreateShaderResourceView(records_.Get(), &records_view, Descriptor(RECORDS));
    device_->CreateShaderResourceView(uvs_.Get(), &uvs_view, Descriptor(UVS));
    device_->CreateShaderResourceView(normals_.Get(), &normals_view, Descriptor(NORMALS));
    device_->CreateShaderResourceView(vertex_buffer_.Get(), &vertices_view, Descriptor(VERTICES));
    const auto records_written = buffer_uav(DXGI_FORMAT_R32G32B32A32_UINT, RECORD_COUNT);
    const auto uvs_written = buffer_uav(DXGI_FORMAT_R32_UINT, uv_pool_);
    const auto normals_written = buffer_uav(DXGI_FORMAT_R32_UINT, NORMAL_POOL);
    device_->CreateUnorderedAccessView(records_.Get(), nullptr, &records_written, Descriptor(RECORDS_WRITTEN));
    device_->CreateUnorderedAccessView(uvs_.Get(), nullptr, &uvs_written, Descriptor(UVS_WRITTEN));
    device_->CreateUnorderedAccessView(normals_.Get(), nullptr, &normals_written, Descriptor(NORMALS_WRITTEN));
    WriteNullPathDescriptors();
    status_ = std::format("Ray tracing ready ({}{}{}; {}{})", dxr12_ ? "DXR 1.2: opacity micromaps" : "DXR 1.1",
                          std::string(path_ser_ ? ", shader execution reordering" : "") + (sharc_path_pipeline_ ? ", SHaRC" : (sharc_failed_.empty() ? "" : ", no SHaRC: its " + sharc_failed_ + " failed"))
                              + (reuse_balanced_pipelines_[0] ? (reuse_balanced_raytraced_pipelines_[0] ? "" : ", no Balanced ReSTIR Light Raytraced: its reuse builds failed")
                                                              : ", no Balanced ReSTIR Light: its reuse builds failed"),
                          (dxr12_ && bake_micromaps_) ? (gpu_micromaps_ ? "; leaf micromaps: NVIDIA OMM SDK (CPU, GPU)"
                                                                        : "; leaf micromaps: NVIDIA OMM SDK (CPU; GPU off: " + gpu_baker + ")")
                                                      : std::string(),
                          sdk,
                          clusters_ ? std::format("; Mega Geometry: room for {} clusters a frame, up to {} ({} from templates of {} bytes), {} MB",
                                                  cluster_room_, cluster_limit_, cluster_room_ - built_limit_, template_bytes_,
                                                  (cluster_bytes_ + TEMPLATE_POOL_BYTES) >> 20)
                                    : std::string());
    slow_report_ = "[TW ray tracing] " + status_
                   + (info_queue_                ? std::string(" (D3D12 debug layer on).")
                      : (debug_result_ != S_FALSE) ? std::format(" (no D3D12 debug layer: 0x{:08X}).", static_cast<uint32_t>(debug_result_))
                                                   : std::string("."));
    sdk_ = sdk;
#if defined(RENODX_REFERENCE)
    slow_report_ += reference_.note;
#endif
    return true;
  }

  // The addon's shared textures and fence (handles still open); false: not all could be opened. `replayable`
  // (main.cpp's RENODX_TW_REPLAYABLE): no shared fence, which Nsight Graphics' replayer cannot make; the caller waits for
  // the addon's value and signals its next, and each request's work is done when it returns.
  bool Open(const dlaa_bridge::Shared& shared, uint32_t version, bool replayable) {
#if defined(RENODX_RECORDER)
    if (recording_.on) recording_.notes += "the textures were opened again (a new size) during the recording; ";
#endif
    Close();
    if (!device_) return false;
    const auto open = [&](uint64_t handle, ComPtr<ID3D12Resource>* resource) {
      return SUCCEEDED(device_->OpenSharedHandle(reinterpret_cast<HANDLE>(handle), IID_PPV_ARGS(resource->GetAddressOf())));
    };
    if (!open(shared.rt_vertices, &vertices_) || !open(shared.rt_positions, &positions_) || !open(shared.rt_sun, &sun_)
        || !open(shared.rt_visibility, &visibility_) || !open(shared.rt_ambient, &ambient_)
        || (!replayable && FAILED(device_->OpenSharedHandle(reinterpret_cast<HANDLE>(shared.fence), IID_PPV_ARGS(fence_.GetAddressOf()))))) {
      Close();
      return false;
    }
    // The leaves' opacity masks (v6; a texture the addon keeps across sizes) and the soft shadows' rays (its size).
    if (version >= 6 && shared.rt_atlas != 0) open(shared.rt_atlas, &atlas_);
    // The ray-traced Light Bounce's (v7): the prepared light in, the bounce out.
    if (version >= 7 && shared.rt_light != 0 && shared.rt_bounce != 0 && open(shared.rt_light, &light_) && open(shared.rt_bounce, &bounce_)) {
      const D3D12_SHADER_RESOURCE_VIEW_DESC light_view = {
          .Format = DXGI_FORMAT_R11G11B10_FLOAT,
          .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
          .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
          .Texture2D = {.MipLevels = 1},
      };
      const D3D12_UNORDERED_ACCESS_VIEW_DESC bounce_view = {.Format = DXGI_FORMAT_R16G16B16A16_FLOAT, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
      device_->CreateShaderResourceView(light_.Get(), &light_view, Descriptor(LIGHT));
      device_->CreateUnorderedAccessView(bounce_.Get(), nullptr, &bounce_view, Descriptor(BOUNCE));
    } else {
      light_.Reset();
      bounce_.Reset();
    }
    // The local lights' lit bits (v9).
    if (version >= 9 && shared.rt_light_mask != 0 && open(shared.rt_light_mask, &light_mask_)) {
      const D3D12_UNORDERED_ACCESS_VIEW_DESC mask_view = {.Format = DXGI_FORMAT_R32_UINT, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
      device_->CreateUnorderedAccessView(light_mask_.Get(), nullptr, &mask_view, Descriptor(LIGHT_MASK));
    }
    // The fog shadows (v11).
    if (version >= 11 && shared.rt_fog != 0 && open(shared.rt_fog, &fog_)) {
      const D3D12_UNORDERED_ACCESS_VIEW_DESC fog_view = {.Format = DXGI_FORMAT_R8_UNORM, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
      device_->CreateUnorderedAccessView(fog_.Get(), nullptr, &fog_view, Descriptor(FOG));
    }
    if (version >= 18 && shared.rt_fog_lights != 0 && open(shared.rt_fog_lights, &fog_lights_)) {
      const D3D12_UNORDERED_ACCESS_VIEW_DESC fog_lights_view = {.Format = DXGI_FORMAT_R16G16B16A16_FLOAT, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
      device_->CreateUnorderedAccessView(fog_lights_.Get(), nullptr, &fog_lights_view, Descriptor(FOG_LIGHTS));
    }
    if (version >= 19 && shared.rt_tint != 0 && open(shared.rt_tint, &tint_)) {
      const D3D12_UNORDERED_ACCESS_VIEW_DESC tint_view = {.Format = DXGI_FORMAT_R8G8B8A8_UNORM, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
      device_->CreateUnorderedAccessView(tint_.Get(), nullptr, &tint_view, Descriptor(TINT));
    }
    // Paths (v8): last frame's scene colour (the DLAA input), the G-buffer guides and the reflections' outputs as the
    // addon shares them (the history of the helper's own made when first used: EnsureHistory).
    if (version >= 8) {
      const auto view = [](DXGI_FORMAT format) {
        return D3D12_SHADER_RESOURCE_VIEW_DESC{.Format = format, .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                                               .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                               .Texture2D = {.MipLevels = 1}};
      };
      if (shared.color != 0 && open(shared.color, &previous_color_)) {
        const auto color_view = view(DXGI_FORMAT_R16G16B16A16_FLOAT);
        device_->CreateShaderResourceView(previous_color_.Get(), &color_view, Descriptor(PREVIOUS_COLOR));
      }
      if (shared.normals != 0 && shared.diffuse_albedo != 0 && shared.roughness != 0 && open(shared.normals, &guide_normals_)
          && open(shared.diffuse_albedo, &guide_albedo_) && open(shared.roughness, &guide_roughness_)) {
        const auto normals_view = view(DXGI_FORMAT_R16G16B16A16_FLOAT);
        const auto albedo_view = view(DXGI_FORMAT_R8G8B8A8_UNORM);
        const auto roughness_view = view(DXGI_FORMAT_R8_UNORM);
        device_->CreateShaderResourceView(guide_normals_.Get(), &normals_view, Descriptor(GUIDE_NORMALS));
        device_->CreateShaderResourceView(guide_albedo_.Get(), &albedo_view, Descriptor(GUIDE_ALBEDO));
        device_->CreateShaderResourceView(guide_roughness_.Get(), &roughness_view, Descriptor(GUIDE_ROUGHNESS));
      } else {
        guide_normals_.Reset();
        guide_albedo_.Reset();
        guide_roughness_.Reset();
      }
      if (shared.rt_reflection != 0 && shared.rt_specular != 0 && open(shared.rt_reflection, &reflection_) && open(shared.rt_specular, &specular_)) {
        Written(reflection_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, REFLECTION);
        Written(specular_.Get(), DXGI_FORMAT_R8_UNORM, SPECULAR_OCCLUSION);
        // Ray Reconstruction's specular hit distance (v17).
        if (version >= 17 && shared.rr_hit_distance != 0 && open(shared.rr_hit_distance, &hit_distance_)) {
          Written(hit_distance_.Get(), DXGI_FORMAT_R16_FLOAT, HIT_DISTANCE);
        }
      } else {
        reflection_.Reset();
        specular_.Reset();
      }
    }
    const D3D12_RESOURCE_DESC size = visibility_->GetDesc();
    const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
    const D3D12_RESOURCE_DESC rays_desc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        .Width = size.Width,
        .Height = size.Height,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .Format = DXGI_FORMAT_R16G16_FLOAT,
        .SampleDesc = {.Count = 1},
        .Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
    };
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rays_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                IID_PPV_ARGS(sun_rays_.GetAddressOf())))) {
      Close();
      return false;
    }
    const D3D12_SHADER_RESOURCE_VIEW_DESC float4_view = {
        .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
        .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        .Texture2D = {.MipLevels = 1},
    };
    const D3D12_SHADER_RESOURCE_VIEW_DESC atlas_view = {
        .Format = DXGI_FORMAT_R8_UNORM,
        .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY,
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        .Texture2DArray = {.MipLevels = 1, .ArraySize = dlaa_bridge::RT_ATLAS_SLICES},
    };
    const D3D12_UNORDERED_ACCESS_VIEW_DESC unorm_view = {.Format = DXGI_FORMAT_R8_UNORM, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
    const D3D12_UNORDERED_ACCESS_VIEW_DESC rays_view = {.Format = DXGI_FORMAT_R16G16_FLOAT, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
    device_->CreateShaderResourceView(positions_.Get(), &float4_view, Descriptor(POSITIONS));
    device_->CreateShaderResourceView(sun_.Get(), &float4_view, Descriptor(SUN));
    device_->CreateShaderResourceView(atlas_.Get(), &atlas_view, Descriptor(ATLAS));
    device_->CreateUnorderedAccessView(visibility_.Get(), nullptr, &unorm_view, Descriptor(VISIBILITY));
    device_->CreateUnorderedAccessView(ambient_.Get(), nullptr, &unorm_view, Descriptor(AMBIENT));
    device_->CreateUnorderedAccessView(sun_rays_.Get(), nullptr, &rays_view, Descriptor(SUN_RAYS));
    return true;
  }

  void Close() {
    WaitForGpuBeforeRelease(own_fence_.Get(), event_, own_value_, FENCE_TIMEOUT, "ray tracing");
    vertices_.Reset();
    positions_.Reset();
    sun_.Reset();
    visibility_.Reset();
    ambient_.Reset();
    atlas_.Reset();
    sun_rays_.Reset();
#if defined(RENODX_NRD)
    for (NrdSignal* signal : {&nrd_sun_, &nrd_ambient_, &nrd_bounce_, &nrd_reflections_}) {
      signal->denoiser.reset();
      signal->failed = false;
    }
    nrd_bounce_hit_distance_.Reset();
    sun_toward_ = {};
    for (Slot& slot : slots_) slot.sun_copied = false;
#endif
    nrd_off_ = 0;
    light_.Reset();
    bounce_.Reset();
    light_mask_.Reset();
    fog_.Reset();
    fog_lights_.Reset();
    tint_.Reset();
    ambient_history_ = {};
    ambient_positions_.Reset();
    light_history_ = {};
    previous_color_.Reset();
    guide_normals_.Reset();
    guide_albedo_.Reset();
    guide_roughness_.Reset();
    kept_positions_.Reset();
    trace_positions_slice_ = NO_SLICE;
    water_positions_.Reset();
    reflection_.Reset();
    specular_.Reset();
    hit_distance_.Reset();
    specular_accumulated_.Reset();
    specular_history_.Reset();
    specular_meta_.Reset();
    specular_meta_history_.Reset();
    reuse_samples_ = {};
    reuse_data_ = {};
    reuse_surface_ = {};
    own_sky_ = {};
    light_ratio_ = {};
    reflect_samples_ = {};
    reflect_data_ = {};
    rtxdi_reservoirs_.Reset();
    rtxdi_offsets_.Reset();
    torch_irradiance_.Reset();
    regir_cells_.Reset();
    if (descriptors_) WriteNullPathDescriptors();
    fence_.Reset();
  }

  // The signals asked for NRD that it did not run on (dlaa_bridge.h rt_nrd_off).
  [[nodiscard]] uint32_t NrdOff() const { return nrd_off_.load(); }

  [[nodiscard]] std::string Status() const {
    // The GPU times right after the label: the bridge's line holds 255 characters, a battle's counts run past them.
    const size_t label = status_.find("): ");
    if (label == std::string::npos) return status_ + cpu_timings_;
    return std::format("{}{}{}{}{}{}", status_.substr(0, label + 3), room_note_, gpu_timings_, gpu_timings_.empty() ? "" : " | ", status_.substr(label + 3), cpu_timings_);
  }
  // A GPU fault or hang (Windows' timeout detection) removed the device: nothing runs on it again.
  [[nodiscard]] HRESULT RemovedReason() const { return device_ ? device_->GetDeviceRemovedReason() : S_OK; }
  [[nodiscard]] bool Removed() const { return FAILED(RemovedReason()); }
  // Removed, or a request could not be recorded: nothing of it ran but the scene's state says it did (new BLASes, cluster
  // sets' data), and a list that failed its Close never resets (debug36: one effect gone for six minutes, then a GPU
  // reset). main.cpp makes a new scene either way.
  [[nodiscard]] bool Lost() const { return request_failed_ || Removed(); }
  // Lost because a request's wait for this queue's own work ran out on a live device (the game's GPU can be seconds
  // behind): main.cpp keeps the scene until Idle instead of freeing it under that work (the destructor's wait would end
  // the helper), and does not count it as a failed request.
  [[nodiscard]] bool TimedOut() const { return timed_out_; }
  // The last wait that timed out: this queue's fence then and the value it waited for.
  struct Stall {
    uint64_t completed = 0;
    uint64_t wanted = 0;
  };
  [[nodiscard]] const Stall& LastStall() const { return stall_; }
  // Nothing submitted can still run (done, or the device removed). Read with no request in flight.
  [[nodiscard]] bool Idle() const { return !own_fence_ || own_fence_->GetCompletedValue() >= own_value_; }

  // Why the device was removed and, from DRED, where: each command list the GPU did not finish, its operations around
  // the one it stopped in (">"; the passes' markers by name), and a page fault's address with the allocations there.
  [[nodiscard]] std::string LostReport() const {
    std::string report = std::format("[TW ray tracing] The GPU removed the device (0x{:08X}).",
                                     static_cast<uint32_t>(device_->GetDeviceRemovedReason()));
    const auto narrow = [](const wchar_t* text) {
      std::string narrowed;
      for (; text != nullptr && *text != L'\0'; ++text) narrowed += static_cast<char>(*text);
      return narrowed;
    };
    ComPtr<ID3D12DeviceRemovedExtendedData1> removed;
    if (FAILED(device_.As(&removed))) return report + " No DRED.";
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs = {};
    if (SUCCEEDED(removed->GetAutoBreadcrumbsOutput1(&breadcrumbs))) {
      for (const D3D12_AUTO_BREADCRUMB_NODE1* node = breadcrumbs.pHeadAutoBreadcrumbNode; node != nullptr; node = node->pNext) {
        const uint32_t done = (node->pLastBreadcrumbValue != nullptr) ? *node->pLastBreadcrumbValue : 0u;
        if (done >= node->BreadcrumbCount) continue;
        const auto slot = std::ranges::find_if(slots_, [&](const Slot& slot) { return slot.list.Get() == node->pCommandList; });
        report += std::format("\n  {}{}: {} of {} operations done:", narrow(node->pCommandListDebugNameW),
                              (slot != slots_.end()) ? " (" + slot->request + slot->micromap_builds + ")" : std::string(), done, node->BreadcrumbCount);
        const uint32_t first = (done > 12 ? done - 12 : 0u);
        auto marker = static_cast<size_t>(std::count(node->pCommandHistory, node->pCommandHistory + first, D3D12_AUTO_BREADCRUMB_OP_SETMARKER));
        for (uint32_t op = first; op < std::min(node->BreadcrumbCount, done + 4); ++op) {
          std::string name;
          for (uint32_t context = 0; context < node->BreadcrumbContextsCount; ++context) {
            if (node->pBreadcrumbContexts[context].BreadcrumbIndex == op) name = narrow(node->pBreadcrumbContexts[context].pContextString);
          }
          if (node->pCommandHistory[op] == D3D12_AUTO_BREADCRUMB_OP_SETMARKER && slot != slots_.end() && marker < slot->marks.size()) {
            name = narrow(slot->marks[marker++]);
          }
          if (name.empty()) {
            const auto known = std::ranges::find(OPERATION_NAMES, node->pCommandHistory[op], &OperationName::op);
            name = (known != OPERATION_NAMES.end()) ? known->name : std::format("op {}", static_cast<int>(node->pCommandHistory[op]));
          }
          report += std::format(" {}{}", (op == done ? ">" : ""), name);
        }
      }
    }
    D3D12_DRED_PAGE_FAULT_OUTPUT1 fault = {};
    if (SUCCEEDED(removed->GetPageFaultAllocationOutput1(&fault))
        && (fault.PageFaultVA != 0 || fault.pHeadExistingAllocationNode != nullptr || fault.pHeadRecentFreedAllocationNode != nullptr)) {
      report += std::format("\n  page fault at 0x{:X}; there:", fault.PageFaultVA);
      for (const D3D12_DRED_ALLOCATION_NODE1* node = fault.pHeadExistingAllocationNode; node != nullptr; node = node->pNext) {
        report += std::format(" {} ({})", narrow(node->ObjectNameW), static_cast<int>(node->AllocationType));
      }
      report += "; freed lately:";
      for (const D3D12_DRED_ALLOCATION_NODE1* node = fault.pHeadRecentFreedAllocationNode; node != nullptr; node = node->pNext) {
        report += std::format(" {} ({})", narrow(node->ObjectNameW), static_cast<int>(node->AllocationType));
      }
    }
    return report;
  }
  [[nodiscard]] bool Dxr12() const { return dxr12_; }
  // The garbage triangles the scene data left out so far (far off; stretched parts: units, grass, cloth; stretched
  // others) and the last capture that had some (its key).
  struct LeftOut {
    uint32_t far_off = 0;
    uint32_t parts = 0;
    uint32_t others = 0;
    uint64_t key = 0;
  };
  [[nodiscard]] const LeftOut& LeftOutSoFar() const { return left_out_; }
  // The log's lines since the last call: a slow request's report and a garbage triangle's example.
  [[nodiscard]] std::string TakeReports() {
    std::string reports = std::exchange(slow_report_, {});
    if (!garbage_example_.empty()) reports += (reports.empty() ? "" : "\n") + std::exchange(garbage_example_, {});
    if (TestBuffers()) {
      reports += std::format("{}[TW ray tracing] Test: {} buffers made since the last report.", (reports.empty() ? "" : "\n"),
                             buffers_made_.exchange(0, std::memory_order_relaxed));
    }
    return reports;
  }

  // Whether it takes compact draws (v16, rt_compact_ok): 0 not, else one more each time the full set pool dropped sets
  // (the addon sends its sets again).
  [[nodiscard]] uint32_t CompactState() const { return clusters_ ? 1u + set_drops_ : 0u; }

  // Its alpha-tested dynamic corners a request (rt_dynamic_uvs).
  [[nodiscard]] uint32_t DynamicUvs() const { return dynamic_uvs_; }

  // The queue its requests go to (main.cpp's frame boundaries for Nsight Graphics).
  [[nodiscard]] ID3D12CommandQueue* Queue() const { return queue_.Get(); }

#if defined(RENODX_RECORDER)
  // The bridge recorder (tests: the recorder build's RENODX_TW_RECORD and "play" command, main.cpp; rt_recording.h;
  // tools/tw/round_notes/plan_1117/ref_step4.md). With RENODX_TW_RECORD set when the scene starts (Initialize), each new
  // static mesh's vertices as its BLAS build read them and each mesh's and cluster set's micromap bake are kept (the
  // journal): a snapshot's BLASes and micromaps are built again from them.
  //
  // The snapshot's plain fields by their names (their names and sizes make FileHeader::layout; first: its deduced type
  // is used below).
  static constexpr auto RecordedFields() {
    return std::tuple{
        std::pair{"next_slot", &RtScene12::next_slot_},
        std::pair{"builds", &RtScene12::builds_},
        std::pair{"set_generation", &RtScene12::set_generation_},
        std::pair{"placed_last", &RtScene12::placed_last_},
        std::pair{"placed_capacity", &RtScene12::placed_capacity_},
        std::pair{"placed_written", &RtScene12::placed_written_},
        std::pair{"placed_rule", &RtScene12::placed_rule_},
        std::pair{"placed_before_rule", &RtScene12::placed_before_rule_},
        std::pair{"placed_rule_traces", &RtScene12::placed_rule_traces_},
        std::pair{"units_record", &RtScene12::units_record_},
        std::pair{"units_masked", &RtScene12::units_masked_},
        std::pair{"clusters", &RtScene12::clusters_},
        std::pair{"dxr12", &RtScene12::dxr12_},
        std::pair{"cluster_room", &RtScene12::cluster_room_},
        std::pair{"built_limit", &RtScene12::built_limit_},
        std::pair{"cluster_limit", &RtScene12::cluster_limit_},
        std::pair{"clusters_crowded", &RtScene12::clusters_crowded_},
        std::pair{"templated_wanted", &RtScene12::templated_wanted_},
        std::pair{"built_wanted", &RtScene12::built_wanted_},
        std::pair{"set_drops", &RtScene12::set_drops_},
        std::pair{"template_bytes", &RtScene12::template_bytes_},
        std::pair{"dynamic_uvs", &RtScene12::dynamic_uvs_},
        std::pair{"ambient_view_projection", &RtScene12::ambient_view_projection_},
        std::pair{"ambient_frame", &RtScene12::ambient_frame_},
        std::pair{"ambient_parity", &RtScene12::ambient_parity_},
        std::pair{"lights_frame", &RtScene12::lights_frame_},
        std::pair{"lights_mode_traced", &RtScene12::lights_mode_traced_},
        std::pair{"previous_lights", &RtScene12::previous_lights_},
        std::pair{"previous_light_count", &RtScene12::previous_light_count_},
        std::pair{"copied_slice", &RtScene12::copied_slice_},
        std::pair{"previous_positions_slice", &RtScene12::previous_positions_slice_},
        std::pair{"trace_positions_slice", &RtScene12::trace_positions_slice_},
        std::pair{"water_frame", &RtScene12::water_frame_},
        std::pair{"path_ambient_frame", &RtScene12::path_ambient_frame_},
        std::pair{"path_kept_cube", &RtScene12::path_kept_cube_},
        std::pair{"path_sun_light", &RtScene12::path_sun_light_},
        std::pair{"kept_copy_wanted", &RtScene12::kept_copy_wanted_},
        std::pair{"kept_copy_frame", &RtScene12::kept_copy_frame_},
        std::pair{"reuse_frame", &RtScene12::reuse_frame_},
        std::pair{"reuse_lights_mode", &RtScene12::reuse_lights_mode_},
        std::pair{"reuse_parity", &RtScene12::reuse_parity_},
        std::pair{"reuse_size", &RtScene12::reuse_size_},
        std::pair{"sky_frame", &RtScene12::sky_frame_},
        std::pair{"sky_parity", &RtScene12::sky_parity_},
        std::pair{"sky_size", &RtScene12::sky_size_},
        std::pair{"ratio_parity", &RtScene12::ratio_parity_},
        std::pair{"ratio_frame", &RtScene12::ratio_frame_},
        std::pair{"ratio_size", &RtScene12::ratio_size_},
        std::pair{"reflect_frame", &RtScene12::reflect_frame_},
        std::pair{"reflect_parity", &RtScene12::reflect_parity_},
        std::pair{"reflect_size", &RtScene12::reflect_size_},
        std::pair{"sharc_frame", &RtScene12::sharc_frame_},
        std::pair{"paths_age", &RtScene12::paths_age_},
        std::pair{"current_view_projection", &RtScene12::current_view_projection_},
        std::pair{"previous_view_projection", &RtScene12::previous_view_projection_},
        std::pair{"paths_frame", &RtScene12::paths_frame_},
        std::pair{"copied_frame", &RtScene12::copied_frame_},
        std::pair{"paths_copied_frame", &RtScene12::paths_copied_frame_},
        std::pair{"light_frame", &RtScene12::light_frame_},
        std::pair{"cache_frame", &RtScene12::cache_frame_},
        std::pair{"specular_frame", &RtScene12::specular_frame_},
        std::pair{"sun_traced_frame", &RtScene12::sun_traced_frame_},
        std::pair{"history_valid", &RtScene12::history_valid_},
        std::pair{"cache_filled", &RtScene12::cache_filled_},
        std::pair{"specular_history_valid", &RtScene12::specular_history_valid_},
        std::pair{"traced", &RtScene12::traced_},
        std::pair{"history_used", &RtScene12::history_used_},
    };
  }

  // The recorded GPU resources (RecordedResources; their names and sizes make FileHeader::layout, in this order): the
  // scene's (records, pools, the placed instances' state, the sets' pool), every history and learned value, the shared
  // outputs.
  static constexpr auto ResourceRows() {
    constexpr D3D12_RESOURCE_STATES WRITTEN = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    constexpr D3D12_RESOURCE_STATES READ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    constexpr D3D12_RESOURCE_STATES COMMON = D3D12_RESOURCE_STATE_COMMON;
    using Member = ComPtr<ID3D12Resource>* (*)(RtScene12*);
    const auto history = [](const char* name, Member member, History kind, uint32_t written_by, std::array<const char*, 3> index = {},
                            D3D12_RESOURCE_STATES state = WRITTEN) {
      return ResourceRow{.name = name, .member = member, .state = state, .made_by = MADE_BY_HISTORY, .history = kind, .written_by = written_by, .index = index};
    };
    const auto cache = [](const char* name, Member member) {
      return ResourceRow{.name = name, .member = member, .state = WRITTEN, .made_by = MADE_BY_CACHE, .written_by = BY_BOUNCE | BY_REFLECTIONS,
                         .index = {"cache_frame", "cache_filled"}};
    };
    const auto output = [](const char* name, Member member, uint32_t written_by) {
      return ResourceRow{.name = name, .member = member, .state = COMMON, .written_by = written_by, .output = true};
    };
    const auto sharc = [](const char* name, Member member) {
      return ResourceRow{.name = name, .member = member, .state = WRITTEN, .made_by = MADE_BY_GROW, .written_by = BY_BOUNCE, .index = {"sharc_frame"}};
    };
    constexpr std::array<const char*, 3> PLACED = {"placed_last", "placed_capacity", "placed_written"};
    constexpr std::array<const char*, 3> SLICES = {"copied_slice", "previous_positions_slice", "trace_positions_slice"};
    return std::array{
        ResourceRow{.name = "records", .member = [](RtScene12* scene) { return std::addressof(scene->records_); }, .state = READ,
                    .bytes = uint64_t{CLUSTER_RECORDS} * 16, .index = {"units_record"}},
        ResourceRow{.name = "uvs", .member = [](RtScene12* scene) { return std::addressof(scene->uvs_); }, .state = READ, .bytes = uint64_t{STATIC_UVS} * 4,
                    .index = {"dynamic_uvs"}},
        ResourceRow{.name = "normals", .member = [](RtScene12* scene) { return std::addressof(scene->normals_); }, .state = READ,
                    .bytes = uint64_t{STATIC_TRIANGLES} * 4},
        ResourceRow{.name = "build_rows", .member = [](RtScene12* scene) { return std::addressof(scene->build_rows_); }, .state = WRITTEN,
                    .index = {"builds"}},
        ResourceRow{.name = "placed_instances_0", .member = [](RtScene12* scene) { return std::addressof(scene->placed_instances_[0]); }, .state = READ,
                    .index = PLACED},
        ResourceRow{.name = "placed_instances_1", .member = [](RtScene12* scene) { return std::addressof(scene->placed_instances_[1]); }, .state = READ,
                    .index = PLACED},
        ResourceRow{.name = "placed_state", .member = [](RtScene12* scene) { return std::addressof(scene->placed_state_); }, .state = WRITTEN, .index = PLACED},
        ResourceRow{.name = "set_pool", .member = [](RtScene12* scene) { return std::addressof(scene->set_pool_); }, .state = READ,
                    .index = {"set_generation"}},
        ResourceRow{.name = "sun_rays", .member = [](RtScene12* scene) { return std::addressof(scene->sun_rays_); }, .state = WRITTEN, .written_by = BY_TRACE,
                    .index = {"sun_traced_frame"}},
        history("ambient_history_0", [](RtScene12* scene) { return std::addressof(scene->ambient_history_[0]); }, HISTORY_AMBIENT, BY_TRACE, {"ambient_parity"}),
        history("ambient_history_1", [](RtScene12* scene) { return std::addressof(scene->ambient_history_[1]); }, HISTORY_AMBIENT, BY_TRACE, {"ambient_parity"}),
        history("ambient_positions", [](RtScene12* scene) { return std::addressof(scene->ambient_positions_); }, HISTORY_AMBIENT, BY_TRACE | BY_REFLECTIONS,
                {"ambient_frame"}, COMMON),
        history("light_history_0", [](RtScene12* scene) { return std::addressof(scene->light_history_[0]); }, HISTORY_LIGHTS, BY_TRACE, {"lights_frame"}),
        history("light_history_1", [](RtScene12* scene) { return std::addressof(scene->light_history_[1]); }, HISTORY_LIGHTS, BY_TRACE, {"lights_frame"}),
        history("kept_positions", [](RtScene12* scene) { return std::addressof(scene->kept_positions_); }, HISTORY_PATHS, BY_TRACE | BY_BOUNCE | BY_REFLECTIONS,
                SLICES, COMMON),
        history("water_positions", [](RtScene12* scene) { return std::addressof(scene->water_positions_); }, HISTORY_WATER, BY_REFLECTIONS, {"water_frame"},
                COMMON),
        history("specular_accumulated", [](RtScene12* scene) { return std::addressof(scene->specular_accumulated_); }, HISTORY_SPECULAR, BY_REFLECTIONS,
                {"specular_history_valid"}),
        history("specular_history", [](RtScene12* scene) { return std::addressof(scene->specular_history_); }, HISTORY_SPECULAR, BY_REFLECTIONS,
                {"specular_history_valid"}),
        history("specular_meta", [](RtScene12* scene) { return std::addressof(scene->specular_meta_); }, HISTORY_SPECULAR, BY_REFLECTIONS,
                {"specular_history_valid"}),
        history("specular_meta_history", [](RtScene12* scene) { return std::addressof(scene->specular_meta_history_); }, HISTORY_SPECULAR, BY_REFLECTIONS,
                {"specular_history_valid"}),
        history("reuse_samples_0", [](RtScene12* scene) { return std::addressof(scene->reuse_samples_[0]); }, HISTORY_REUSE, BY_BOUNCE, {"reuse_parity"}),
        history("reuse_samples_1", [](RtScene12* scene) { return std::addressof(scene->reuse_samples_[1]); }, HISTORY_REUSE, BY_BOUNCE, {"reuse_parity"}),
        history("reuse_data_0", [](RtScene12* scene) { return std::addressof(scene->reuse_data_[0]); }, HISTORY_REUSE, BY_BOUNCE, {"reuse_parity"}),
        history("reuse_data_1", [](RtScene12* scene) { return std::addressof(scene->reuse_data_[1]); }, HISTORY_REUSE, BY_BOUNCE, {"reuse_parity"}),
        history("reuse_surface_0", [](RtScene12* scene) { return std::addressof(scene->reuse_surface_[0]); }, HISTORY_REUSE, BY_BOUNCE, {"reuse_parity"}),
        history("reuse_surface_1", [](RtScene12* scene) { return std::addressof(scene->reuse_surface_[1]); }, HISTORY_REUSE, BY_BOUNCE, {"reuse_parity"}),
        history("own_sky_0", [](RtScene12* scene) { return std::addressof(scene->own_sky_[0]); }, HISTORY_SKY, BY_BOUNCE, {"sky_parity"}),
        history("own_sky_1", [](RtScene12* scene) { return std::addressof(scene->own_sky_[1]); }, HISTORY_SKY, BY_BOUNCE, {"sky_parity"}),
        history("light_ratio_0", [](RtScene12* scene) { return std::addressof(scene->light_ratio_[0]); }, HISTORY_RATIO, BY_BOUNCE, {"ratio_parity"}),
        history("light_ratio_1", [](RtScene12* scene) { return std::addressof(scene->light_ratio_[1]); }, HISTORY_RATIO, BY_BOUNCE, {"ratio_parity"}),
        history("reflect_samples_0", [](RtScene12* scene) { return std::addressof(scene->reflect_samples_[0]); }, HISTORY_REFLECT, BY_REFLECTIONS,
                {"reflect_parity"}),
        history("reflect_samples_1", [](RtScene12* scene) { return std::addressof(scene->reflect_samples_[1]); }, HISTORY_REFLECT, BY_REFLECTIONS,
                {"reflect_parity"}),
        history("reflect_data_0", [](RtScene12* scene) { return std::addressof(scene->reflect_data_[0]); }, HISTORY_REFLECT, BY_REFLECTIONS, {"reflect_parity"}),
        history("reflect_data_1", [](RtScene12* scene) { return std::addressof(scene->reflect_data_[1]); }, HISTORY_REFLECT, BY_REFLECTIONS, {"reflect_parity"}),
        cache("cache_keys", [](RtScene12* scene) { return std::addressof(scene->cache_keys_); }),
        cache("cache_radiance", [](RtScene12* scene) { return std::addressof(scene->cache_radiance_); }),
        cache("cache_sums", [](RtScene12* scene) { return std::addressof(scene->cache_sums_); }),
        cache("cache_albedo", [](RtScene12* scene) { return std::addressof(scene->cache_albedo_); }),
        cache("light_fit", [](RtScene12* scene) { return std::addressof(scene->light_fit_); }),
        cache("light_kept", [](RtScene12* scene) { return std::addressof(scene->light_kept_); }),
        sharc("sharc_0", [](RtScene12* scene) { return std::addressof(scene->sharc_buffers_[0]); }),
        sharc("sharc_1", [](RtScene12* scene) { return std::addressof(scene->sharc_buffers_[1]); }),
        sharc("sharc_2", [](RtScene12* scene) { return std::addressof(scene->sharc_buffers_[2]); }),
        output("visibility", [](RtScene12* scene) { return std::addressof(scene->visibility_); }, BY_TRACE),
        output("ambient", [](RtScene12* scene) { return std::addressof(scene->ambient_); }, BY_TRACE),
        output("light_mask", [](RtScene12* scene) { return std::addressof(scene->light_mask_); }, BY_TRACE),
        output("fog", [](RtScene12* scene) { return std::addressof(scene->fog_); }, BY_TRACE),
        output("fog_lights", [](RtScene12* scene) { return std::addressof(scene->fog_lights_); }, BY_TRACE),
        output("tint", [](RtScene12* scene) { return std::addressof(scene->tint_); }, BY_TRACE),
        output("bounce", [](RtScene12* scene) { return std::addressof(scene->bounce_); }, BY_BOUNCE),
        output("reflection", [](RtScene12* scene) { return std::addressof(scene->reflection_); }, BY_REFLECTIONS),
        output("specular", [](RtScene12* scene) { return std::addressof(scene->specular_); }, BY_REFLECTIONS),
        output("hit_distance", [](RtScene12* scene) { return std::addressof(scene->hit_distance_); }, BY_REFLECTIONS),
    };
  }

  // The resource-use log's names (UseName).
  static constexpr auto UseNames() {
    using Member = ComPtr<ID3D12Resource>* (*)(RtScene12*);
    const auto nrd = [](const char* name, UINT descriptor) { return UseName{.name = name, .descriptor = descriptor}; };
    const auto buffer = [](const char* name, Member member, UINT sets = 1) { return UseName{.name = name, .member = member, .sets = sets}; };
    const auto nvapi = [](const char* name, Member member, UINT sets = 1) { return UseName{.name = name, .member = member, .sets = sets, .nvapi = true}; };
    return std::array{
        UseName{"positions", POSITIONS, [](RtScene12* scene) { return std::addressof(scene->positions_); }},
        UseName{"sun", SUN, [](RtScene12* scene) { return std::addressof(scene->sun_); }},
        UseName{"records", RECORDS, [](RtScene12* scene) { return std::addressof(scene->records_); }},
        UseName{"uvs", UVS, [](RtScene12* scene) { return std::addressof(scene->uvs_); }},
        UseName{"normals", NORMALS, [](RtScene12* scene) { return std::addressof(scene->normals_); }},
        UseName{"atlas", ATLAS, [](RtScene12* scene) { return std::addressof(scene->atlas_); }},
        UseName{"vertices", VERTICES, [](RtScene12* scene) { return std::addressof(scene->vertex_buffer_); }},
        UseName{"visibility", VISIBILITY, [](RtScene12* scene) { return std::addressof(scene->visibility_); }},
        UseName{"ambient", AMBIENT, [](RtScene12* scene) { return std::addressof(scene->ambient_); }},
        UseName{"sun_rays", SUN_RAYS, [](RtScene12* scene) { return std::addressof(scene->sun_rays_); }},
        UseName{"records", RECORDS_WRITTEN, [](RtScene12* scene) { return std::addressof(scene->records_); }},
        UseName{"uvs", UVS_WRITTEN, [](RtScene12* scene) { return std::addressof(scene->uvs_); }},
        UseName{"normals", NORMALS_WRITTEN, [](RtScene12* scene) { return std::addressof(scene->normals_); }},
        UseName{"light", LIGHT, [](RtScene12* scene) { return std::addressof(scene->light_); }},
        UseName{"bounce", BOUNCE, [](RtScene12* scene) { return std::addressof(scene->bounce_); }},
        UseName{"previous_color", PREVIOUS_COLOR, [](RtScene12* scene) { return std::addressof(scene->previous_color_); }},
        UseName{"kept_positions", PREVIOUS_POSITIONS, [](RtScene12* scene) { return std::addressof(scene->kept_positions_); }},
        UseName{"guide_normals", GUIDE_NORMALS, [](RtScene12* scene) { return std::addressof(scene->guide_normals_); }},
        UseName{"guide_albedo", GUIDE_ALBEDO, [](RtScene12* scene) { return std::addressof(scene->guide_albedo_); }},
        UseName{"guide_roughness", GUIDE_ROUGHNESS, [](RtScene12* scene) { return std::addressof(scene->guide_roughness_); }},
        UseName{"cache_keys", CACHE_KEYS, [](RtScene12* scene) { return std::addressof(scene->cache_keys_); }},
        UseName{"cache_radiance", CACHE_RADIANCE, [](RtScene12* scene) { return std::addressof(scene->cache_radiance_); }},
        UseName{"cache_sums", CACHE_SUMS, [](RtScene12* scene) { return std::addressof(scene->cache_sums_); }},
        UseName{"cache_albedo", CACHE_ALBEDO, [](RtScene12* scene) { return std::addressof(scene->cache_albedo_); }},
        UseName{"light_fit", LIGHT_FIT, [](RtScene12* scene) { return std::addressof(scene->light_fit_); }},
        UseName{"light_kept", LIGHT_KEPT, [](RtScene12* scene) { return std::addressof(scene->light_kept_); }},
        UseName{"specular_accumulated", SPECULAR_ACCUMULATED, [](RtScene12* scene) { return std::addressof(scene->specular_accumulated_); }},
        UseName{"specular_history", SPECULAR_HISTORY, [](RtScene12* scene) { return std::addressof(scene->specular_history_); }},
        UseName{"specular_meta", SPECULAR_META, [](RtScene12* scene) { return std::addressof(scene->specular_meta_); }},
        UseName{"specular_meta_history", SPECULAR_META_HISTORY, [](RtScene12* scene) { return std::addressof(scene->specular_meta_history_); }},
        UseName{"reflection", REFLECTION, [](RtScene12* scene) { return std::addressof(scene->reflection_); }},
        UseName{"specular", SPECULAR_OCCLUSION, [](RtScene12* scene) { return std::addressof(scene->specular_); }},
        UseName{"light_mask", LIGHT_MASK, [](RtScene12* scene) { return std::addressof(scene->light_mask_); }},
        UseName{"reuse_samples", REUSE_SAMPLES, [](RtScene12* scene) { return scene->reuse_samples_.data(); }, 2},
        UseName{"reuse_data", REUSE_DATA, [](RtScene12* scene) { return scene->reuse_data_.data(); }, 2},
        UseName{"fog", FOG, [](RtScene12* scene) { return std::addressof(scene->fog_); }},
        UseName{"ambient_history", AMBIENT_HISTORY, [](RtScene12* scene) { return scene->ambient_history_.data(); }, 2},
        UseName{"ambient_positions", AMBIENT_POSITIONS, [](RtScene12* scene) { return std::addressof(scene->ambient_positions_); }},
        UseName{"hit_distance", HIT_DISTANCE, [](RtScene12* scene) { return std::addressof(scene->hit_distance_); }},
        UseName{"light_history", LIGHT_HISTORY, [](RtScene12* scene) { return scene->light_history_.data(); }, 2},
        UseName{"fog_lights", FOG_LIGHTS, [](RtScene12* scene) { return std::addressof(scene->fog_lights_); }},
        UseName{"tint", TINT, [](RtScene12* scene) { return std::addressof(scene->tint_); }},
        UseName{"reflect_samples", REFLECT_SAMPLES, [](RtScene12* scene) { return scene->reflect_samples_.data(); }, 2},
        UseName{"reflect_data", REFLECT_DATA, [](RtScene12* scene) { return scene->reflect_data_.data(); }, 2},
        nrd("nrd_sun_view_z", NRD_SUN_VIEW_Z),
        nrd("nrd_sun_normal_roughness", NRD_SUN_NORMAL_ROUGHNESS),
        nrd("nrd_sun_penumbra", NRD_SUN_PENUMBRA),
        nrd("nrd_sun_translucency", NRD_SUN_TRANSLUCENCY),
        nrd("nrd_bounce_hit_distance", NRD_BOUNCE_HIT_DISTANCE),
        nrd("nrd_bounce_view_z", NRD_BOUNCE_VIEW_Z),
        nrd("nrd_bounce_normal_roughness", NRD_BOUNCE_NORMAL_ROUGHNESS),
        nrd("nrd_bounce_signal", NRD_BOUNCE_SIGNAL),
        nrd("nrd_reflect_view_z", NRD_REFLECT_VIEW_Z),
        nrd("nrd_reflect_normal_roughness", NRD_REFLECT_NORMAL_ROUGHNESS),
        nrd("nrd_reflect_signal", NRD_REFLECT_SIGNAL),
        nrd("nrd_ambient_view_z", NRD_AMBIENT_VIEW_Z),
        nrd("nrd_ambient_normal_roughness", NRD_AMBIENT_NORMAL_ROUGHNESS),
        nrd("nrd_ambient_signal", NRD_AMBIENT_SIGNAL),
        UseName{"own_sky", OWN_SKY, [](RtScene12* scene) { return scene->own_sky_.data(); }, 2},
        UseName{"light_ratio", LIGHT_RATIO, [](RtScene12* scene) { return scene->light_ratio_.data(); }, 2},
        UseName{"reuse_surface_0", SURFACE_ORIGIN, [](RtScene12* scene) { return std::addressof(scene->reuse_surface_[0]); }},
        UseName{"reuse_surface_1", SURFACE_NORMAL, [](RtScene12* scene) { return std::addressof(scene->reuse_surface_[1]); }},
        buffer("tlas", [](RtScene12* scene) { return std::addressof(scene->tlas_); }),
        buffer("tlas_scratch", [](RtScene12* scene) { return std::addressof(scene->tlas_scratch_); }),
        buffer("scratch", [](RtScene12* scene) { return std::addressof(scene->scratch_); }),
        buffer("left_out_start", [](RtScene12* scene) { return std::addressof(scene->left_out_start_); }),
        buffer("smoke_tlas", [](RtScene12* scene) { return std::addressof(scene->smoke_tlas_); }),
        buffer("smoke_tlas_scratch", [](RtScene12* scene) { return std::addressof(scene->smoke_tlas_scratch_); }),
        buffer("placed_scratch", [](RtScene12* scene) { return std::addressof(scene->placed_scratch_); }),
        buffer("units_blas", [](RtScene12* scene) { return std::addressof(scene->units_blas_); }),
        buffer("smoke_blas", [](RtScene12* scene) { return std::addressof(scene->smoke_blas_); }),
        nvapi("clas", [](RtScene12* scene) { return std::addressof(scene->clas_); }),
        nvapi("clas_addresses", [](RtScene12* scene) { return std::addressof(scene->clas_addresses_); }),
        nvapi("cluster_blases", [](RtScene12* scene) { return scene->cluster_blases_.data(); }, CLUSTER_BLASES),
        nvapi("cluster_destination", [](RtScene12* scene) { return std::addressof(scene->cluster_destination_); }),
        nvapi("cluster_scratch", [](RtScene12* scene) { return std::addressof(scene->cluster_scratch_); }),
        nvapi("cluster_indices", [](RtScene12* scene) { return std::addressof(scene->cluster_indices_); }),
        nvapi("cluster_args", [](RtScene12* scene) { return std::addressof(scene->cluster_args_); }),
        nvapi("templates", [](RtScene12* scene) { return std::addressof(scene->templates_); }),
        nvapi("template_destinations", [](RtScene12* scene) { return std::addressof(scene->template_destinations_); }),
        nvapi("template_geometry", [](RtScene12* scene) { return std::addressof(scene->template_geometry_); }),
        nvapi("sway_arguments", [](RtScene12* scene) { return std::addressof(scene->sway_arguments_); }),
        nvapi("sway_lists", [](RtScene12* scene) { return std::addressof(scene->sway_lists_); }),
        nvapi("sway_counts", [](RtScene12* scene) { return std::addressof(scene->sway_counts_); }),
        nvapi("ptlas_buffers", [](RtScene12* scene) { return scene->ptlas_buffers_.data(); }, 2),
        nvapi("ptlas_scratch", [](RtScene12* scene) { return std::addressof(scene->ptlas_scratch_); }),
        buffer("cache_owners", [](RtScene12* scene) { return std::addressof(scene->cache_owners_); }),
        buffer("vertices_copy", [](RtScene12* scene) { return std::addressof(scene->vertices_); }),
        buffer("path_ser_table", [](RtScene12* scene) { return std::addressof(scene->path_ser_table_); }),
    };
  }

  // The scene between two frames (none of its work on its way) into `folder`/state.bin, `frame` the first recorded: its
  // plain fields, the static meshes with their journal, the cluster sets, the pools' free ranges, every history, learned
  // value and shared output. False: not now (`why`: micromap bakes, row reads or a room on their way; again at the next
  // trace) or never (`*refused` too).
  bool SaveRecordingState(const std::filesystem::path& folder, uint32_t frame, std::string* why, bool* refused) {
    *refused = true;
    snapshot_waited_ = 0;
    if (!device_ || !visibility_ || Lost()) {
      *why = "no ray tracing scene";
      return false;
    }
    if (!journal_on_) {
      *why = "the journal is off (RENODX_TW_RECORD was not set when this scene started)";
      return false;
    }
    if (ptlas_ != nullptr) {
      *why = "the partitioned TLAS (RENODX_TW_PTLAS) is not recorded";
      return false;
    }
    if (!WaitFor(own_value_)) {
      *why = "the GPU did not finish the last requests";
      return false;
    }
    *refused = false;
    if (!Settled(why)) {
      snapshot_waited_ = std::max<uint64_t>(own_value_, 1);
      return false;
    }
    *refused = true;
    for (const auto& [key, mesh] : meshes_) {
      const auto kept = journal_.find(key);
      if (kept == journal_.end() || kept->second.vertices.size() != uint64_t{mesh.triangles} * 3) {
        *why = std::format("mesh {:016X} has no journal (captured before RENODX_TW_RECORD was set?)", key);
        return false;
      }
      if (const auto baked = micromap_journal_.find(key); baked != micromap_journal_.end() && baked->second.build == mesh.build && baked->second.unread) {
        *why = std::format("mesh {:016X}'s GPU micromap bake was not read back for the journal", key);
        return false;
      }
    }
    for (const auto& [key, set] : sets_) {
      const auto baked = set_journal_.find(key);
      const bool journaled = baked != set_journal_.end() && baked->second.build == set.generation;
      if ((set.micromaps && !journaled) || (journaled && baked->second.unread)) {
        *why = std::format("cluster set {:016X}'s micromaps have no bake in the journal", key);
        return false;
      }
    }
    *refused = false;
    if (placed_slot_ != SLOTS) {
      placed_written_ = slots_[placed_slot_].left_out_data[LEFT_OUT_BYTES / 4];
      placed_slot_ = SLOTS;
    }
    // The units' BLAS (without Mega Geometry) built anew at the next trace here as in the player (a refit's BVH is not
    // the build's).
    units_layout_.clear();
    units_refits_ = 0;
    std::vector<uint8_t> chunks;
    std::apply([&](const auto&... field) { (AddChunk(&chunks, recording::CHUNK_FIELD, field.first, std::as_bytes(std::span(&(this->*field.second), 1))), ...); },
               RecordedFields());
    const auto add_ranges = [&](const char* name, const Ranges& ranges) {
      std::vector<std::array<uint32_t, 2>> free;
      for (const auto& [offset, count] : ranges.free) free.push_back({offset, count});
      AddChunk(&chunks, recording::CHUNK_FIELD, name, std::as_bytes(std::span(free)));
    };
    add_ranges("free_triangles", free_triangles_);
    add_ranges("free_uvs", free_uvs_);
    add_ranges("free_instances", free_instances_);
    add_ranges("free_set_units", free_set_units_);
    add_ranges("free_templates", free_templates_);
    AddChunk(&chunks, recording::CHUNK_FIELD, "free_records", std::as_bytes(std::span(free_records_)));
    std::vector<std::array<uint32_t, 6>> frees;
    for (const PendingFree& pending : pending_frees_) {
      frees.push_back({pending.record, pending.normal_offset, pending.triangles, pending.uv_offset, pending.first_instance, pending.instances});
    }
    AddChunk(&chunks, recording::CHUNK_FIELD, "pending_frees", std::as_bytes(std::span(frees)));
    std::vector<std::array<uint32_t, 4>> set_frees;
    for (const PendingSetFree& pending : pending_set_frees_) set_frees.push_back({pending.offset, pending.units, pending.template_first, pending.templates});
    AddChunk(&chunks, recording::CHUNK_FIELD, "pending_set_frees", std::as_bytes(std::span(set_frees)));
    for (const auto& [key, mesh] : meshes_) {
      const Journal& kept = journal_.at(key);
      const auto baked = micromap_journal_.find(key);
      const Journal* micromaps = (baked != micromap_journal_.end() && baked->second.build == mesh.build) ? &baked->second : nullptr;
      const MeshRecord record = {
          .key = key, .build = mesh.build, .drawn_frame = mesh.drawn_frame, .triangles = mesh.triangles, .record = mesh.record,
          .normal_offset = mesh.normal_offset, .uv_offset = mesh.uv_offset, .first_instance = mesh.first_instance,
          .flags = (mesh.terrain ? MESH_TERRAIN : 0u) | (mesh.foliage ? MESH_FOLIAGE : 0u) | (mesh.grass ? MESH_GRASS : 0u)
                   | (mesh.solid_cut_out ? MESH_CUT_OUT : 0u) | (mesh.placed ? MESH_PLACED : 0u) | (mesh.moved ? MESH_MOVED : 0u)
                   | (micromaps != nullptr ? MESH_MICROMAPS : 0u),
          .transforms = static_cast<uint32_t>(mesh.transforms.size()),
      };
      AddChunk(&chunks, recording::CHUNK_MESH, "mesh",
               JoinBytes({std::as_bytes(std::span(&record, 1)), std::as_bytes(std::span(mesh.transforms)), std::as_bytes(std::span(kept.vertices))}, micromaps));
    }
    for (const auto& [key, set] : sets_) {
      const auto baked = set_journal_.find(key);
      const Journal* micromaps = (set.micromaps && baked != set_journal_.end() && baked->second.build == set.generation) ? &baked->second : nullptr;
      const SetRecord record = {.key = key, .generation = set.generation, .offset = set.offset, .units = set.units, .vertices = set.vertices,
                                .clusters = set.clusters, .triangles = set.triangles, .template_first = set.template_first,
                                .flags = (set.micromaps_tried ? SET_TRIED : 0u) | (micromaps != nullptr ? SET_MICROMAPS : 0u)
                                         | (set.template_masked ? SET_TEMPLATE_MASKED : 0u)};
      AddChunk(&chunks, recording::CHUNK_SET, "set", JoinBytes({std::as_bytes(std::span(&record, 1)), std::as_bytes(std::span(set.descs))}, micromaps));
    }
    ID3D12GraphicsCommandList4* list = RecorderList();
    if (list == nullptr) {
      *why = "no command list for the recorder";
      return false;
    }
    std::vector<Readback> readbacks;
    for (const RecordedResource& resource : RecordedResources()) {
      if (*resource.member) CopyToReadback(list, resource.name, resource.member->Get(), resource.state, &readbacks, resource.offset, resource.bytes);
    }
    size_t denoisers = 0;
#if defined(RENODX_NRD)
    // NRD's denoisers: each one's textures' size and last frame's fields ("nrd<signal>"), its history ("nrd<signal>_<n>").
    for (size_t signal = 0; signal < NrdSignals().size(); ++signal) {
      const NrdSignal& nrd = *NrdSignals()[signal];
      if (!nrd.denoiser) continue;
      const Nrd12::Kept kept = nrd.denoiser->Saved();
      AddChunk(&chunks, recording::CHUNK_FIELD, std::format("nrd{}", signal),
               JoinBytes({std::as_bytes(std::span(nrd.size)), std::as_bytes(std::span(&kept, 1))}, nullptr));
      const auto history = nrd.denoiser->History();
      for (size_t index = 0; index < history.size(); ++index) {
        CopyToReadback(list, std::format("nrd{}_{}", signal, index).c_str(), history[index].first, history[index].second, &readbacks);
      }
      ++denoisers;
    }
#endif
    *why = std::format("state: {} meshes, {} cluster sets, {} GPU resources, {} NRD denoisers", meshes_.size(), sets_.size(), readbacks.size(), denoisers);
    std::error_code ignored;
    std::filesystem::create_directories(folder, ignored);
    const uint64_t value = SubmitRecorderList(list);
    QueueRecordingJob(value, [this, path = folder / L"state.bin", header = RecordingHeader(frame, 0), chunks = std::move(chunks),
                              readbacks = std::move(readbacks)]() mutable { WriteRecordingFile(path, header, chunks, &readbacks, false); });
    return true;
  }

  // The player's RENODX_TW_PLAY_MICROMAP_LEVEL / RENODX_TW_PLAY_LEAF_MICROMAP_LEVEL (LoadRecordingState): grass's / leaves'
  // micromaps baked again at that level (-1: built from their recorded bakes), from the first request's block, vertex
  // texture and atlas (its chunks' bytes; no block: nothing baked again).
  struct MicromapRebake {
    int grass_level = -1;
    int leaf_level = -1;
    const dlaa_bridge::Shared* block = nullptr;
    std::span<const uint8_t> vertices;
    std::span<const uint8_t> atlas;
  };
  // `folder`/state.bin into this scene before its first request (a scene the harness's player opened at the recording's
  // textures' size), `*first_frame` the recording's first frame. False: refused (`why`: another layout or bridge version,
  // a truncated file, a device or texture size the recording does not fit); the scene stays as it was. `rebake`: the
  // micromaps baked again (MicromapRebake; `why` says how many and whether their bytes are the recorded ones).
  bool LoadRecordingState(const std::filesystem::path& folder, const MicromapRebake& rebake, uint32_t* first_frame, std::string* why) {
    if (!device_ || !visibility_ || meshes_.size() != 0) {
      *why = "no new ray tracing scene to load it into";
      return false;
    }
    std::error_code error;
    const std::filesystem::path path = folder / L"state.bin";
    const uint64_t size = std::filesystem::file_size(path, error);
    std::vector<uint8_t> data(error ? 0u : size);
    if (std::ifstream file(path, std::ios::binary); error || !file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()))) {
      *why = std::format("{} could not be read", path.string());
      return false;
    }
    recording::FileHeader header = {};
    if (data.size() < sizeof(header)) {
      *why = "state.bin is truncated (no header)";
      return false;
    }
    std::memcpy(&header, data.data(), sizeof(header));
    if (header.magic != recording::MAGIC || header.format != recording::FORMAT || header.bridge_version < dlaa_bridge::OLDEST_VERSION
        || header.bridge_version > dlaa_bridge::VERSION || header.layout != RecordingLayout()) {
      *why = std::format("state.bin is another build's (format {} bridge v{} layout {:016X}, written by {}; this helper: format {} bridge v{} layout {:016X})",
                         header.format, header.bridge_version, header.layout, std::string_view(header.helper, strnlen(header.helper, sizeof(header.helper))),
                         recording::FORMAT, dlaa_bridge::VERSION, RecordingLayout());
      return false;
    }
    // Its chunks by name (fields, resources) and kind (meshes, sets).
    std::unordered_map<std::string, std::span<const uint8_t>> fields;
    std::unordered_map<std::string, std::span<const uint8_t>> resources;
    std::vector<std::span<const uint8_t>> mesh_chunks;
    std::vector<std::span<const uint8_t>> set_chunks;
    bool ended = false;
    for (uint64_t at = sizeof(header); !ended;) {
      recording::ChunkHeader chunk = {};
      if (at + sizeof(chunk) > data.size()) break;
      std::memcpy(&chunk, data.data() + at, sizeof(chunk));
      at += sizeof(chunk);
      if (at + chunk.name_bytes + chunk.bytes > data.size()) break;
      const std::string name(reinterpret_cast<const char*>(data.data() + at), chunk.name_bytes);
      const std::span<const uint8_t> bytes(data.data() + at + chunk.name_bytes, chunk.bytes);
      at += chunk.name_bytes + chunk.bytes;
      switch (chunk.kind) {
        case recording::CHUNK_END:
          ended = true;
          break;
        case recording::CHUNK_FIELD:
          fields[name] = bytes;
          break;
        case recording::CHUNK_TEXTURE:
        case recording::CHUNK_BUFFER:
          resources[name] = bytes;
          break;
        case recording::CHUNK_MESH:
          mesh_chunks.push_back(bytes);
          break;
        case recording::CHUNK_SET:
          set_chunks.push_back(bytes);
          break;
        default:
          break;
      }
    }
    if (!ended) {
      *why = "state.bin is truncated (no end chunk)";
      return false;
    }
    bool fitting = true;
    std::apply([&](const auto&... field) { ((fitting = fitting && fields.contains(field.first) && fields[field.first].size() == sizeof(this->*field.second)), ...); },
               RecordedFields());
    if (!fitting) {
      *why = "state.bin's fields do not fit this helper";
      return false;
    }
    // The device's own choices must be the recording's (the pools' sizes and the passes depend on them).
    const auto recorded = [&](const char* name, auto* value) { std::memcpy(value, fields[name].data(), sizeof(*value)); };
    bool recorded_clusters = false;
    bool recorded_dxr12 = false;
    uint32_t recorded_template_bytes = 0;
    uint32_t recorded_uvs = 0;
    uint32_t recorded_capacity = 0;
    uint32_t recorded_room = 0;
    uint32_t recorded_built = 0;
    recorded("clusters", &recorded_clusters);
    recorded("dxr12", &recorded_dxr12);
    recorded("template_bytes", &recorded_template_bytes);
    recorded("dynamic_uvs", &recorded_uvs);
    recorded("placed_capacity", &recorded_capacity);
    recorded("cluster_room", &recorded_room);
    recorded("built_limit", &recorded_built);
    if (recorded_clusters != clusters_ || recorded_dxr12 != dxr12_ || recorded_template_bytes != template_bytes_ || recorded_uvs != dynamic_uvs_) {
      *why = std::format("recorded on another device: Mega Geometry {} / {}, DXR 1.2 {} / {}, template bytes {} / {}, alpha-tested corners {} / {} (recording / here)",
                         recorded_clusters, clusters_, recorded_dxr12, dxr12_, recorded_template_bytes, template_bytes_, recorded_uvs, dynamic_uvs_);
      return false;
    }
    if (recorded_capacity > placed_limit_) {
      *why = std::format("its {} placed instances' room is past this GPU's {}", recorded_capacity, placed_limit_);
      return false;
    }
    if (PlacedRoom room; recorded_capacity != placed_capacity_) {
      if (!MakePlacedRoom(recorded_capacity, &room)) {
        *why = "no memory for the placed instances' room";
        return false;
      }
      UsePlacedRoom(&room);
    }
#if defined(RENODX_NVAPI_CLUSTERS)
    if (ClusterRoom room; clusters_ && (recorded_room != cluster_room_ || recorded_built != built_limit_)) {
      if (!SizeClusterRoom(recorded_room, recorded_built, &room) || !MakeClusterRoom(&room)) {
        *why = "no memory for the clusters' room";
        return false;
      }
      UseClusterRoom(&room);
    }
#endif
    // The histories the recording has, made as their first use makes them (then their fields as recorded).
    for (const RecordedResource& resource : RecordedResources()) {
      if (!resources.contains(resource.name) || *resource.member) continue;
      bool made = false;
      recording::BufferHeader grown = {};
      switch (resource.made_by) {
        case MADE_BY_HISTORY:
          made = EnsureHistory(resource.history, 0);
          break;
        case MADE_BY_CACHE:
          made = EnsureCache();
          break;
        case MADE_BY_GROW:
          std::memcpy(&grown, resources[resource.name].data(), std::min(sizeof(grown), resources[resource.name].size()));
          made = GrowBuffer(resource.member, grown.bytes, resource.state);
          break;
        default:
          break;
      }
      if (!made) {
        *why = std::format("{} could not be made here", resource.name);
        return false;
      }
    }
    std::apply([&](const auto&... field) { (std::memcpy(&(this->*field.second), fields[field.first].data(), sizeof(this->*field.second)), ...); }, RecordedFields());
    const auto ranges = [&](const char* name, Ranges* into) {
      const std::span<const uint8_t> bytes = fields[name];
      into->free.clear();
      for (size_t at = 0; at + 8 <= bytes.size(); at += 8) {
        std::array<uint32_t, 2> range = {};
        std::memcpy(range.data(), bytes.data() + at, 8);
        into->free.emplace(range[0], range[1]);
      }
    };
    ranges("free_triangles", &free_triangles_);
    ranges("free_uvs", &free_uvs_);
    ranges("free_instances", &free_instances_);
    ranges("free_set_units", &free_set_units_);
    ranges("free_templates", &free_templates_);
    free_records_.resize(fields["free_records"].size() / 4);
    std::memcpy(free_records_.data(), fields["free_records"].data(), free_records_.size() * 4);
    // Given back at the first trace, as the live scene gives them back there (its GPU was past them).
    pending_frees_.clear();
    for (size_t at = 0; at + 24 <= fields["pending_frees"].size(); at += 24) {
      std::array<uint32_t, 6> free = {};
      std::memcpy(free.data(), fields["pending_frees"].data() + at, 24);
      pending_frees_.push_back({.value = 0, .record = free[0], .normal_offset = free[1], .triangles = free[2], .uv_offset = free[3],
                                .first_instance = free[4], .instances = free[5]});
    }
    pending_set_frees_.clear();
    for (size_t at = 0; at + 16 <= fields["pending_set_frees"].size(); at += 16) {
      std::array<uint32_t, 4> free = {};
      std::memcpy(free.data(), fields["pending_set_frees"].data() + at, 16);
      pending_set_frees_.push_back({.value = 0, .offset = free[0], .units = free[1], .template_first = free[2], .templates = free[3]});
    }
    // The units' BLAS (without Mega Geometry) is built at the first trace (the live one refitted its own).
    units_layout_.clear();
    units_refits_ = 0;
#if defined(RENODX_NRD)
    // NRD's denoisers as recorded, kept until EnsureNrd makes each one here.
    for (size_t signal = 0; signal < recorded_nrd_.size(); ++signal) {
      recorded_nrd_[signal].reset();
      const auto found = fields.find(std::format("nrd{}", signal));
      if (found == fields.end() || found->second.size() != sizeof(RecordedNrd::size) + sizeof(Nrd12::Kept)) continue;
      RecordedNrd& nrd = recorded_nrd_[signal].emplace();
      std::memcpy(nrd.size.data(), found->second.data(), sizeof(nrd.size));
      std::memcpy(&nrd.kept, found->second.data() + sizeof(nrd.size), sizeof(nrd.kept));
      for (auto texture = resources.find(std::format("nrd{}_0", signal)); texture != resources.end();
           texture = resources.find(std::format("nrd{}_{}", signal, nrd.history.size()))) {
        nrd.history.emplace_back(texture->second.begin(), texture->second.end());
      }
    }
#endif
    why->clear();
    ID3D12GraphicsCommandList4* list = RecorderList();
    if (list == nullptr) {
      *why = "no command list for the player";
      return false;
    }
    // The GPU resources' contents, then the meshes' BLASes (with their micromaps) and the sets' micromaps, in lists of up to
    // ~512 MB of uploads and scratch each (`more`: a new list after; the last one left closed).
    std::vector<ComPtr<ID3D12Resource>> uploads;
    uint64_t batch_bytes = 0;
    const auto flush = [&](bool more = true) {
      const uint64_t value = SubmitRecorderList(list);
      const bool done = WaitFor(value);
      uploads.clear();
      batch_bytes = 0;
      list = (done && more) ? RecorderList() : nullptr;
      return done && (!more || list != nullptr);
    };
    for (const RecordedResource& resource : RecordedResources()) {
      const auto found = resources.find(resource.name);
      if (found == resources.end() || !*resource.member || list == nullptr) continue;
      // A ReSTIR history recorded in the other reservoir layout (RT_RESERVOIR_COMPACT): left out, its sets empty (M 0).
      if ((resource.history == HISTORY_REUSE || resource.history == HISTORY_REFLECT) && found->second.size() >= sizeof(recording::TextureHeader)) {
        recording::TextureHeader recorded = {};
        std::memcpy(&recorded, found->second.data(), sizeof(recorded));
        if (recorded.format != static_cast<uint32_t>((*resource.member)->GetDesc().Format)) continue;
      }
      if (!UploadRecorded(list, resource.name, resource.member->Get(), resource.state, found->second, &uploads, why)) break;
      batch_bytes += found->second.size();
      if (batch_bytes > (512ull << 20) && !flush()) break;
    }
    // MicromapRebake's kinds baked again as a live bake bakes them (the recording's baker, BakeMicromapsOnGpu or
    // BakeMicromaps, at the given level) from their recorded inputs: a mesh's UVs and record (atlas slice, cut-off) in the
    // pools; a cluster set's first instance in the first request (its draw's first vertex, slice and flags, its corners'
    // UVs in the vertex texture as rt_compact writes them); the atlas slice the first request's. A set the first request
    // does not draw, or a failed bake, keeps its recorded bake.
    std::unordered_map<const uint8_t*, std::vector<std::byte>> rebaked;  // by chunk: its bake's bytes (MicromapBytes)
    std::string rebake_report;
#if defined(RENODX_OMM_SDK)
    if (rebake.block != nullptr && list != nullptr && why->empty()) {
      const auto started = std::chrono::steady_clock::now();
      if (rebake.grass_level >= 0) grass_micromap_level_ = rebake.grass_level;
      if (rebake.leaf_level >= 0) leaf_micromap_level_ = rebake.leaf_level;
      // A recorded buffer's `bytes` at `offset` (nullptr: not recorded there).
      const auto buffer_part = [&](const char* name, uint64_t offset, uint64_t bytes) -> const uint8_t* {
        const auto found = resources.find(name);
        recording::BufferHeader part = {};
        if (found == resources.end() || found->second.size() < sizeof(part)) return nullptr;
        std::memcpy(&part, found->second.data(), sizeof(part));
        if (offset < part.offset || offset - part.offset + bytes > found->second.size() - sizeof(part)) return nullptr;
        return found->second.data() + sizeof(part) + (offset - part.offset);
      };
      // Each atlas slice's mask (its first mip) in the first request's bytes.
      constexpr uint64_t MASK_BYTES = uint64_t{dlaa_bridge::RT_ATLAS_SIZE} * dlaa_bridge::RT_ATLAS_SIZE;
      std::vector<const uint8_t*> masks;
      if (recording::TextureHeader atlas = {}; atlas_ && rebake.atlas.size() >= sizeof(atlas)) {
        std::memcpy(&atlas, rebake.atlas.data(), sizeof(atlas));
        const D3D12_RESOURCE_DESC atlas_desc = atlas_->GetDesc();
        const UINT subresources = atlas_desc.DepthOrArraySize * atlas_desc.MipLevels;
        std::vector<UINT> rows(subresources);
        std::vector<UINT64> row_bytes(subresources);
        device_->GetCopyableFootprints(&atlas_desc, 0, subresources, 0, nullptr, rows.data(), row_bytes.data(), nullptr);
        uint64_t at = sizeof(atlas);
        for (UINT subresource = 0; subresource < subresources; ++subresource) {
          if (subresource % atlas_desc.MipLevels == 0) masks.push_back(rebake.atlas.data() + at);
          at += uint64_t{rows[subresource]} * row_bytes[subresource];
        }
        if (at > rebake.atlas.size() || atlas.slices != subresources || uint64_t{rows[0]} * row_bytes[0] != MASK_BYTES) masks.clear();
      }
      recording::TextureHeader vertices = {};
      if (rebake.vertices.size() >= sizeof(vertices)) std::memcpy(&vertices, rebake.vertices.data(), sizeof(vertices));
      // A vertex's UV (its w: two halves) in the first request's vertex texture; false: not recorded.
      const auto vertex_uv = [&](uint64_t vertex, uint32_t* uv) {
        if (vertices.width == 0) return false;
        const uint64_t row = vertex / vertices.width;
        const uint64_t at = sizeof(vertices) + ((row - vertices.first_row) * vertices.row_bytes) + ((vertex % vertices.width) * 16) + 12;
        if (row < vertices.first_row || row - vertices.first_row >= vertices.rows || at + 4 > rebake.vertices.size()) return false;
        std::memcpy(uv, rebake.vertices.data() + at, 4);
        return true;
      };
      struct Rebaked {
        const uint8_t* chunk = nullptr;
        std::span<const uint8_t> recorded;  // its recorded bake (MicromapBytes on)
        MicromapBake* bake = nullptr;       // owned by `baking` (GPU bakes: by the scene's lists until AdvanceMicromaps)
      };
      std::vector<Rebaked> rebakes;
      std::vector<std::unique_ptr<MicromapBake>> baking;
      uint32_t kept = 0;  // their inputs not recorded (a set: not drawn in the first request)
      const auto add = [&](const uint8_t* chunk, std::span<const uint8_t> recorded, std::unique_ptr<MicromapBake> bake, uint32_t slice) {
        if (slice >= masks.size()) {
          ++kept;
          return;
        }
        bake->mask_data.assign(masks[slice], masks[slice] + MASK_BYTES);
        rebakes.push_back({.chunk = chunk, .recorded = recorded, .bake = bake.get()});
        baking.push_back(std::move(bake));
      };
      for (const std::span<const uint8_t> chunk : mesh_chunks) {
        MeshRecord record = {};
        if (chunk.size() < sizeof(record)) continue;
        std::memcpy(&record, chunk.data(), sizeof(record));
        const bool grass = (record.flags & MESH_GRASS) != 0;
        const uint64_t bake_at = sizeof(record) + (uint64_t{record.transforms} * 48) + (uint64_t{record.triangles} * 48);
        if ((record.flags & MESH_MICROMAPS) == 0 || (grass ? rebake.grass_level : rebake.leaf_level) < 0 || chunk.size() < bake_at) continue;
        const uint8_t* uvs = buffer_part("uvs", uint64_t{record.uv_offset} * 4, uint64_t{record.triangles} * 12);
        const uint8_t* entry = buffer_part("records", uint64_t{record.record} * 16, 16);
        if (uvs == nullptr || entry == nullptr) {
          ++kept;
          continue;
        }
        // Its record: UV offset, normal offset, atlas slice | flags << 16, its alpha cut-off (rt_scene_data).
        std::array<uint32_t, 4> placement = {};
        std::memcpy(placement.data(), entry, sizeof(placement));
        auto bake = std::make_unique<MicromapBake>();
        *bake = {.key = record.key, .build = record.build, .triangles = record.triangles, .grass = grass, .cutoff = std::bit_cast<float>(placement[3])};
        bake->uv_data.resize(uint64_t{record.triangles} * 3);
        std::memcpy(bake->uv_data.data(), uvs, uint64_t{record.triangles} * 12);
        add(chunk.data(), chunk.subspan(bake_at), std::move(bake), placement[2] & 0xFFFFu);
      }
      std::unordered_map<uint64_t, const dlaa_bridge::RtCompactDraw*> set_draws;  // each set's first draw in the first request
      for (uint32_t index = 0; index < std::min(rebake.block->rt_compact_count, dlaa_bridge::RT_COMPACT_DRAWS); ++index) {
        set_draws.try_emplace(rebake.block->rt_compact[index].set, &rebake.block->rt_compact[index]);
      }
      // Trace's cut-off (but RENODX_TW_OWN_CUTOFF's).
      const float own_cutoff = std::clamp((rebake.block->rt_leaf_cutoff > 0.f) ? rebake.block->rt_leaf_cutoff : LEAF_ALPHA_CUTOFF, 0.f, 2.f);
      for (const std::span<const uint8_t> chunk : set_chunks) {
        SetRecord record = {};
        if (chunk.size() < sizeof(record)) continue;
        std::memcpy(&record, chunk.data(), sizeof(record));
        const uint64_t bake_at = sizeof(record) + (uint64_t{record.clusters} * 16);
        if ((record.flags & SET_MICROMAPS) == 0 || chunk.size() < bake_at) continue;
        const auto drawn = set_draws.find(record.key);
        const uint8_t* corners = buffer_part("set_pool", (uint64_t{record.offset} + record.clusters) * 16, uint64_t{record.triangles} * 3);
        if (drawn == set_draws.end() || corners == nullptr) {
          ++kept;
          continue;
        }
        const dlaa_bridge::RtCompactDraw& draw = *drawn->second;
        const bool grass = (draw.flags & dlaa_bridge::RT_GRASS) != 0;
        if ((grass ? rebake.grass_level : rebake.leaf_level) < 0) continue;
        auto bake = std::make_unique<MicromapBake>();
        *bake = {.key = record.key, .build = record.generation, .triangles = record.triangles, .set = true, .grass = grass,
                 .cutoff = ((draw.flags & dlaa_bridge::RT_OWN_CUTOFF) != 0) ? own_cutoff : LEAF_ALPHA_CUTOFF};
        bake->uv_data.resize(uint64_t{record.triangles} * 3);
        // Each cluster's triangles (its desc: first vertex in the block, vertices, first triangle, triangles), their corners
        // three bytes each.
        bool read = true;
        for (uint32_t cluster = 0; cluster < record.clusters && read; ++cluster) {
          std::array<uint32_t, 4> desc = {};
          std::memcpy(desc.data(), chunk.data() + sizeof(record) + (uint64_t{cluster} * 16), sizeof(desc));
          for (uint64_t corner = uint64_t{desc[2]} * 3; corner < (uint64_t{desc[2]} + desc[3]) * 3 && read; ++corner) {
            read = corner < bake->uv_data.size() && vertex_uv(uint64_t{draw.first_vertex} + desc[0] + corners[corner], &bake->uv_data[corner]);
          }
        }
        if (!read) {
          ++kept;
          continue;
        }
        add(chunk.data(), chunk.subspan(bake_at), std::move(bake), draw.slice);
      }
      const bool gpu = gpu_micromaps_ && (rebake.block->rt_flags & dlaa_bridge::RT_GPU_MICROMAPS) != 0;
      if (gpu) {
        // A request's GPU bakes at most (BakeMicromapsOnGpu's budget) a list, their UVs and masks uploaded; their outputs
        // read back as the journal's (AdvanceMicromaps).
        for (size_t next = 0; next < rebakes.size() && list != nullptr;) {
          size_t end = next;
          uint32_t batch_triangles = 0;
          uint64_t uv_bytes = 0;
          while (end < rebakes.size() && end - next < MICROMAP_GPU_BAKES
                 && (end == next || batch_triangles + rebakes[end].bake->triangles <= MICROMAP_GPU_TRIANGLES)) {
            batch_triangles += rebakes[end].bake->triangles;
            uv_bytes = Align(uv_bytes + (uint64_t{rebakes[end].bake->triangles} * 12), 256);
            ++end;
          }
          ComPtr<ID3D12Resource> uvs = Buffer(uv_bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
          ComPtr<ID3D12Resource> mask_upload = Buffer((end - next) * MASK_BYTES, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
          uint8_t* uv_data = nullptr;
          uint8_t* mask_data = nullptr;
          if (!uvs || !mask_upload || FAILED(uvs->Map(0, nullptr, reinterpret_cast<void**>(&uv_data)))
              || FAILED(mask_upload->Map(0, nullptr, reinterpret_cast<void**>(&mask_data)))) {
            break;
          }
          std::vector<MicromapBake*> batch;
          uint64_t uvs_at = 0;
          for (size_t index = next; index < end; ++index) {
            MicromapBake& bake = *rebakes[index].bake;
            const auto mask_index = static_cast<uint32_t>(index - next);
            auto& gpu_masks = omm_gpu_.masks;
            if (mask_index == gpu_masks.size()) {
              const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
              const D3D12_RESOURCE_DESC texture = {.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D, .Width = dlaa_bridge::RT_ATLAS_SIZE,
                                                   .Height = dlaa_bridge::RT_ATLAS_SIZE, .DepthOrArraySize = 1, .MipLevels = 1,
                                                   .Format = DXGI_FORMAT_R8_UNORM, .SampleDesc = {.Count = 1}};
              ComPtr<ID3D12Resource> made;
              if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                                          nullptr, IID_PPV_ARGS(made.GetAddressOf())))) {
                break;
              }
              gpu_masks.push_back(std::move(made));
            }
            std::memcpy(uv_data + uvs_at, bake.uv_data.data(), uint64_t{bake.triangles} * 12);
            std::memcpy(mask_data + (uint64_t{mask_index} * MASK_BYTES), bake.mask_data.data(), MASK_BYTES);
            ID3D12Resource* gpu_mask = gpu_masks[mask_index].Get();
            const auto to_copy = Transition(gpu_mask, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
            list->ResourceBarrier(1, &to_copy);
            const D3D12_TEXTURE_COPY_LOCATION destination = {.pResource = gpu_mask, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, .SubresourceIndex = 0};
            const D3D12_TEXTURE_COPY_LOCATION source = {
                .pResource = mask_upload.Get(),
                .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
                .PlacedFootprint = {.Offset = uint64_t{mask_index} * MASK_BYTES,
                                    .Footprint = {.Format = DXGI_FORMAT_R8_UNORM, .Width = dlaa_bridge::RT_ATLAS_SIZE, .Height = dlaa_bridge::RT_ATLAS_SIZE,
                                                  .Depth = 1, .RowPitch = dlaa_bridge::RT_ATLAS_SIZE}},
            };
            list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            const auto copied = Transition(gpu_mask, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            list->ResourceBarrier(1, &copied);
            bake.gpu = true;
            bake.value = own_value_ + 1;  // the flush's
            bake.uvs = uvs;
            bake.uvs_offset = uvs_at;
            bake.mask_index = mask_index;
            uvs_at = Align(uvs_at + (uint64_t{bake.triangles} * 12), 256);
            batch.push_back(&bake);
          }
          uvs->Unmap(0, nullptr);
          mask_upload->Unmap(0, nullptr);
          // The heap, root signature and table a request's bakes find (Trace's).
          ID3D12DescriptorHeap* heaps[] = {descriptors_.Get()};
          list->SetDescriptorHeaps(1, heaps);
          list->SetComputeRootSignature(root_signature_.Get());
          list->SetComputeRootDescriptorTable(ROOT_TABLE, descriptors_->GetGPUDescriptorHandleForHeapStart());
          BakeMicromapsOnGpu(list, batch);
          for (MicromapBake* bake : batch) {
            const auto owned = std::ranges::find(baking, bake, [](const auto& entry) { return entry.get(); });
            micromap_reads_.push_back(std::move(*owned));
          }
          uploads.push_back(std::move(uvs));
          uploads.push_back(std::move(mask_upload));
          if (!flush()) break;
          AdvanceMicromaps(own_fence_->GetCompletedValue());
          baking.insert(baking.end(), std::make_move_iterator(micromaps_baked_.begin()), std::make_move_iterator(micromaps_baked_.end()));
          micromaps_baked_.clear();
          // A mask's texture failed: the rest keep their recorded bakes.
          if (batch.size() != end - next) break;
          next = end;
        }
      } else if (bake_micromaps_) {
        // MICROMAP_WORKERS bakes at once, each with its baker (as AdvanceMicromaps runs them).
        std::array<std::future<void>, MICROMAP_WORKERS> workers;
        for (size_t worker = 0; worker < workers.size(); ++worker) {
          workers[worker] = std::async(std::launch::async, [&, worker] {
            for (size_t index = worker; index < rebakes.size(); index += MICROMAP_WORKERS) BakeMicromaps(rebakes[index].bake, omm_.bakers[worker]);
          });
        }
        for (const auto& worker : workers) worker.wait();
      }
      // The bakes' bytes as the recorder writes them (JoinBytes), beside the recorded ones.
      uint32_t same = 0;
      uint32_t failed = 0;
      std::array<uint64_t, 2> data_bytes = {};  // recorded, baked again
      for (const Rebaked& entry : rebakes) {
        MicromapBytes sizes = {};
        std::memcpy(&sizes, entry.recorded.data(), std::min(sizeof(sizes), entry.recorded.size()));
        data_bytes[0] += sizes.data;
        const auto owned = std::ranges::find(baking, entry.bake, [](const auto& bake) { return bake.get(); });
        if (owned == baking.end() || entry.bake->indices.size() != entry.bake->triangles) {
          ++failed;
          continue;
        }
        const Journal journal = {.data = std::move(entry.bake->data), .descs = std::move(entry.bake->descs), .indices = std::move(entry.bake->indices),
                                 .histogram = std::move(entry.bake->histogram)};
        std::vector<std::byte> bytes = JoinBytes({}, &journal);
        const auto recorded = std::as_bytes(entry.recorded);
        same += (recorded.size() >= bytes.size() && std::ranges::equal(recorded.first(bytes.size()), bytes)) ? 1 : 0;
        data_bytes[1] += journal.data.size();
        rebaked[entry.chunk] = std::move(bytes);
      }
      const auto level_text = [](int level) { return (level >= 0) ? std::to_string(level) : std::string("as recorded"); };
      rebake_report = std::format(
          "; micromaps baked again ({} baker; grass level {}, leaves' {}): {} bakes, {} of them the recorded bytes, {} not (from "
          "{} data bytes to {}); {} kept as recorded ({} not drawn in the first request or inputs not recorded, {} failed); {:.1f} ms",
          gpu ? "GPU" : "CPU", level_text(rebake.grass_level), level_text(rebake.leaf_level), rebaked.size(), same, rebaked.size() - same, data_bytes[0],
          data_bytes[1], kept + failed, kept, failed, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
    }
#endif
    // A chunk's bake from `at`: its recorded one, or the one baked again.
    const auto bake_bytes = [&](std::span<const uint8_t> chunk, uint64_t at) {
      const auto found = rebaked.find(chunk.data());
      if (found == rebaked.end()) return chunk.subspan(at);
      return std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(found->second.data()), found->second.size());
    };
    // A mesh's bytes: its record, its transforms, its vertices, then its micromaps' (MicromapBytes); its BLAS built as
    // Record builds a static one (its geometry's flags, PREFER_FAST_TRACE, not compacted: the same hits).
    const D3D12_RESOURCE_BARRIER uav_barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV};
    for (const std::span<const uint8_t> chunk : mesh_chunks) {
      if (list == nullptr || !why->empty()) break;
      MeshRecord record = {};
      if (chunk.size() < sizeof(record)) continue;
      std::memcpy(&record, chunk.data(), sizeof(record));
      const uint64_t transforms_bytes = uint64_t{record.transforms} * 48;
      const uint64_t vertices_bytes = uint64_t{record.triangles} * 48;
      if (chunk.size() < sizeof(record) + transforms_bytes + vertices_bytes) {
        *why = "state.bin is truncated (a mesh)";
        break;
      }
      Mesh& mesh = meshes_[record.key];
      mesh = {.build = record.build, .drawn_frame = record.drawn_frame, .triangles = record.triangles, .record = record.record,
              .normal_offset = record.normal_offset, .uv_offset = record.uv_offset, .terrain = (record.flags & MESH_TERRAIN) != 0,
              .foliage = (record.flags & MESH_FOLIAGE) != 0, .grass = (record.flags & MESH_GRASS) != 0, .solid_cut_out = (record.flags & MESH_CUT_OUT) != 0,
              .first_instance = record.first_instance, .placed = (record.flags & MESH_PLACED) != 0, .moved = (record.flags & MESH_MOVED) != 0};
      mesh.transforms.resize(record.transforms);
      std::memcpy(mesh.transforms.data(), chunk.data() + sizeof(record), transforms_bytes);
      ComPtr<ID3D12Resource> vertices = Buffer(vertices_bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
      uint8_t* mapped = nullptr;
      if (!vertices || FAILED(vertices->Map(0, nullptr, reinterpret_cast<void**>(&mapped)))) {
        *why = "no memory for a mesh's vertices";
        break;
      }
      std::memcpy(mapped, chunk.data() + sizeof(record) + transforms_bytes, vertices_bytes);
      vertices->Unmap(0, nullptr);
      D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC triangles = {.IndexFormat = DXGI_FORMAT_UNKNOWN, .VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
                                                            .VertexCount = record.triangles * 3,
                                                            .VertexBuffer = {.StartAddress = vertices->GetGPUVirtualAddress(), .StrideInBytes = 16}};
      D3D12_RAYTRACING_GEOMETRY_DESC geometry = {
          .Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES,
          .Flags = mesh.foliage ? D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION : D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE,
          .Triangles = triangles,
      };
      D3D12_RAYTRACING_GEOMETRY_OMM_LINKAGE_DESC linkage = {};
      if ((record.flags & MESH_MICROMAPS) != 0) {
        ComPtr<ID3D12Resource> inputs;
        uint64_t indices_offset = 0;
        if (!BuildRecordedMicromaps(list, bake_bytes(chunk, sizeof(record) + transforms_bytes + vertices_bytes), std::addressof(mesh.micromaps), std::addressof(inputs), &indices_offset,
                                    &uploads)) {
          *why = "a mesh's micromaps could not be built";
          break;
        }
        list->ResourceBarrier(1, &uav_barrier);
        linkage = {.OpacityMicromapIndexBuffer = {.StartAddress = inputs->GetGPUVirtualAddress() + indices_offset, .StrideInBytes = 4},
                   .OpacityMicromapIndexFormat = DXGI_FORMAT_R32_UINT, .OpacityMicromapBaseLocation = 0,
                   .OpacityMicromapArray = mesh.micromaps ? mesh.micromaps->GetGPUVirtualAddress() : 0};
        geometry = {.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_OMM_TRIANGLES, .Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION,
                    .OmmTriangles = {.pTriangles = &triangles, .pOmmLinkage = &linkage}};
        uploads.push_back(std::move(inputs));
      }
      const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
          .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
          .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION,
          .NumDescs = 1,
          .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
          .pGeometryDescs = &geometry,
      };
      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
      device_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
      mesh.blas = PlaceBlas(info.ResultDataMaxSizeInBytes, KEPT_POOL);
      mesh.bytes = info.ResultDataMaxSizeInBytes;
      ComPtr<ID3D12Resource> scratch = Buffer(std::max<uint64_t>(info.ScratchDataSizeInBytes, 256), D3D12_HEAP_TYPE_DEFAULT,
                                              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      if (mesh.blas.address == 0 || !scratch) {
        *why = "no memory for a mesh's BLAS";
        break;
      }
      const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build = {.DestAccelerationStructureData = mesh.blas.address, .Inputs = inputs,
                                                                         .ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress()};
      list->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
      uploads.push_back(std::move(vertices));
      uploads.push_back(std::move(scratch));
      batch_bytes += vertices_bytes + info.ScratchDataSizeInBytes;
      if (batch_bytes > (512ull << 20) && !flush()) break;
    }
    // A set's bytes: its record, its clusters' descs, then its micromaps' (MicromapBytes).
    for (const std::span<const uint8_t> chunk : set_chunks) {
      if (list == nullptr || !why->empty()) break;
      SetRecord record = {};
      if (chunk.size() < sizeof(record)) continue;
      std::memcpy(&record, chunk.data(), sizeof(record));
      if (chunk.size() < sizeof(record) + (uint64_t{record.clusters} * 16)) {
        *why = "state.bin is truncated (a cluster set)";
        break;
      }
      ClusterSet& set = sets_[record.key];
      set = {.offset = record.offset, .units = record.units, .vertices = record.vertices, .clusters = record.clusters, .triangles = record.triangles,
             .template_first = record.template_first, .template_masked = (record.flags & SET_TEMPLATE_MASKED) != 0, .generation = record.generation,
             .micromaps_tried = (record.flags & SET_TRIED) != 0};
      set.descs.resize(record.clusters);
      std::memcpy(set.descs.data(), chunk.data() + sizeof(record), uint64_t{record.clusters} * 16);
      if ((record.flags & SET_MICROMAPS) != 0
          && !BuildRecordedMicromaps(list, bake_bytes(chunk, sizeof(record) + (uint64_t{record.clusters} * 16)), std::addressof(set.micromaps), std::addressof(set.micromap_inputs),
                                     &set.micromap_indices, &uploads)) {
        *why = "a cluster set's micromaps could not be built";
      }
    }
#if defined(RENODX_NVAPI_CLUSTERS)
    // The sets' templates at their recorded places, built as Record builds them (its arguments' fields; TEMPLATE_BUILDS
    // a list, from the first slot's upload before any request), after their micromaps.
    if (clusters_ && list != nullptr && why->empty()) {
      list->ResourceBarrier(1, &uav_barrier);
      Slot& upload_slot = slots_[0];
      auto* template_args =
          reinterpret_cast<NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_TEMPLATE_ARGS*>(upload_slot.compact_data + COMPACT_TEMPLATE_ARGS_OFFSET);
      auto* template_destinations = reinterpret_cast<uint64_t*>(upload_slot.compact_data + COMPACT_TEMPLATE_DESTINATIONS_OFFSET);
      uint32_t template_builds = 0;
      const auto build_templates = [&] {
        std::memcpy(upload_slot.compact_data + COMPACT_TEMPLATE_COUNT_OFFSET, &template_builds, sizeof(template_builds));
        const auto to_copy = Transition(template_destinations_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        list->ResourceBarrier(1, &to_copy);
        list->CopyBufferRegion(template_destinations_.Get(), 0, upload_slot.compact.Get(), COMPACT_TEMPLATE_DESTINATIONS_OFFSET, uint64_t{template_builds} * 8);
        const auto copied = Transition(template_destinations_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        list->ResourceBarrier(1, &copied);
        const uint64_t upload = upload_slot.compact->GetGPUVirtualAddress();
        const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC templates_desc = {
            .inputs = template_inputs_,
            .addressResolutionFlags = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_ADDRESS_RESOLUTION_FLAG_NONE,
            .batchResultData = 0,
            .batchScratchData = cluster_scratch_->GetGPUVirtualAddress(),
            .destinationAddressArray = {.StartAddress = template_destinations_->GetGPUVirtualAddress(), .StrideInBytes = 8},
            .resultSizeArray = {},
            .indirectArgArray = {.StartAddress = upload + COMPACT_TEMPLATE_ARGS_OFFSET,
                                 .StrideInBytes = sizeof(NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_TEMPLATE_ARGS)},
            .indirectArgCount = upload + COMPACT_TEMPLATE_COUNT_OFFSET,
        };
        const NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS templates_params = {
            .version = NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS_VER, .pDesc = &templates_desc};
        const NvAPI_Status built = NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation(list, &templates_params);
        list->ResourceBarrier(1, &uav_barrier);
        template_builds = 0;
        if (built != NVAPI_OK) {
          *why = std::format("the sets' templates could not be built (NvAPI {})", static_cast<int>(built));
        }
        return built == NVAPI_OK && flush();
      };
      for (const auto& [key, set] : sets_) {
        if (set.template_first == UINT32_MAX) continue;
        if (template_builds + set.clusters > TEMPLATE_BUILDS && !build_templates()) break;
        for (uint32_t cluster = 0; cluster < set.clusters; ++cluster) {
          const auto& [desc_vertex, desc_vertices, desc_triangle, desc_triangles] = set.descs[cluster];
          auto& arg = template_args[template_builds];
          arg = {};
          arg.triangleCount = desc_triangles;
          arg.vertexCount = desc_vertices;
          arg.indexFormat = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INDEX_FORMAT_8BIT;
          arg.baseGeometryIndexAndFlags = set.template_masked ? NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION
                                                              : NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_GEOMETRY_FLAG_OPAQUE;
          arg.indexBufferStride = 1;
          arg.geometryIndexAndFlagsBufferStride = 4;
          arg.geometryIndexAndFlagsBuffer = template_geometry_->GetGPUVirtualAddress();
          arg.indexBuffer = set_pool_->GetGPUVirtualAddress() + ((uint64_t{set.offset} + set.clusters) * 16) + (uint64_t{desc_triangle} * 3);
          if (set.micromaps) {
            arg.opacityMicromapIndexFormat = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INDEX_FORMAT_32BIT;
            arg.opacityMicromapIndexBufferStride = 4;
            arg.opacityMicromapArray = set.micromaps->GetGPUVirtualAddress();
            arg.opacityMicromapIndexBuffer = set.micromap_inputs->GetGPUVirtualAddress() + set.micromap_indices + (uint64_t{desc_triangle} * 4);
          }
          template_destinations[template_builds] = templates_->GetGPUVirtualAddress() + (uint64_t{set.template_first + cluster} * template_bytes_);
          ++template_builds;
        }
      }
      if (template_builds != 0 && why->empty()) build_templates();
    }
#endif
    const bool loaded = why->empty() && list != nullptr && flush(false);
    if (!loaded) {
      if (why->empty()) *why = "the player's GPU work failed";
      return false;
    }
    *first_frame = header.frame;
    *why = std::format("state of frame {} loaded: {} meshes, {} cluster sets, {} GPU resources", header.frame, meshes_.size(), sets_.size(), resources.size()) + rebake_report;
    return true;
  }

  // The requests of frames `first` to `last` into `folder`: a request file each (its inputs only with `inputs`), its outputs,
  // with `histories` at `last` the histories its kind writes too (~2.5 GB at 4K), and each trace's scene digest
  // (RecordRequest), named by their frames `name_shift` earlier. (The writer's fields, `bytes`, `failed` and `hashes`, stay
  // until FinishRecording: a snapshot's file may still be on its way.)
  void RecordFrames(const std::filesystem::path& folder, uint32_t first, uint32_t last, bool inputs, bool histories, uint32_t name_shift = 0) {
    recording_.on = true;
    recording_.name_shift = name_shift;
    recording_.inputs = inputs;
    recording_.histories = histories;
    recording_.first = first;
    recording_.last = last;
    recording_.folder = folder;
    recording_.frame = UINT32_MAX;
    recording_.requests = 0;
    std::error_code ignored;
    std::filesystem::create_directories(folder, ignored);
  }

  // The recording's files all written (its writer waited for); its summary for the log (complete.txt beside them when no
  // request was left out and the device stayed).
  std::string FinishRecording() {
    recording_.on = false;
    if (recording_writer_.valid()) recording_writer_.wait();
    const std::string failed = recording_.notes + recording_.failed;
    const std::string summary = std::format("{} requests of frames {} to {} written to {}, {} MB{}", recording_.requests, recording_.first, recording_.last,
                                            recording_.folder.string(), recording_.bytes >> 20, failed.empty() ? std::string() : "; INCOMPLETE: " + failed);
    if (failed.empty() && recording_.requests != 0) std::ofstream(recording_.folder / L"complete.txt") << summary << "\n";
    recording_ = {};
    return summary;
  }

  // The player (tests: main.cpp's "play" and the resident's "serve", rt_serve.h): `recording`'s requests on this scene, just
  // initialized: textures of its own as the recording's (shared handles opened as the addon's), its state loaded
  // (LoadRecordingState; `micromap_levels`: grass's and leaves' micromaps baked again at those levels, -1 as recorded,
  // MicromapRebake), each request's inputs staged once in video memory (`progress`: the percent staged). False: `report`
  // says why.
  bool LoadPlayback(const std::filesystem::path& recording, std::array<int, 2> micromap_levels, const std::function<void(uint32_t)>& progress,
                    std::string* report) {
    player_ = true;
    playback_ = {};
    std::error_code error;
    std::vector<std::tuple<uint32_t, uint32_t, std::filesystem::path>> requests;
    for (const auto& entry : std::filesystem::directory_iterator(recording, error)) {
      const std::string name = entry.path().filename().string();
      uint32_t frame = 0;
      uint32_t index = 0;
      if (name.starts_with("request_") && sscanf_s(name.c_str(), "request_%u_%u_", &frame, &index) == 2) requests.emplace_back(frame, index, entry.path());
    }
    std::ranges::sort(requests);
    if (requests.empty()) {
      *report = std::format("no requests in {}", recording.string());
      return false;
    }
    if (!std::filesystem::exists(recording / L"complete.txt", error)) {
      *report = std::format("{} has no complete.txt: its recording did not finish (the helper's log says why)", recording.string());
      return false;
    }
    // The shared textures: the inputs as the first request has them (every input then), the outputs as the state has them.
    PlaybackFile first;
    PlaybackFile state;
    if (!ReadPlaybackFile(std::get<2>(requests.front()), &first, report) || !ReadPlaybackFile(recording / L"state.bin", &state, report)) return false;
    if (first.block.size() < offsetof(dlaa_bridge::Shared, generation)) {
      *report = "the first request has no block";
      return false;
    }
    std::memcpy(&playback_.version, first.block.data(), sizeof(playback_.version));
    std::vector<HANDLE> opened;
    for (const auto* resources : {&first.resources, &state.resources}) {
      for (const auto& [name, bytes] : *resources) {
        const auto handle = std::ranges::find(PLAYER_HANDLES, std::string_view(name), [](const auto& entry) { return std::string_view(entry.first); });
        recording::TextureHeader texture = {};
        if (handle == PLAYER_HANDLES.end() || playback_.textures.contains(name) || bytes.size() < sizeof(texture)) continue;
        std::memcpy(&texture, bytes.data(), sizeof(texture));
        const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
        const D3D12_RESOURCE_DESC desc = {.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D, .Width = texture.width, .Height = texture.height,
                                          .DepthOrArraySize = static_cast<UINT16>(texture.slices / std::max(texture.mips, 1u)),
                                          .MipLevels = static_cast<UINT16>(std::max(texture.mips, 1u)), .Format = static_cast<DXGI_FORMAT>(texture.format),
                                          .SampleDesc = {.Count = 1}, .Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS};
        HANDLE shared_handle = nullptr;
        if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                    IID_PPV_ARGS(playback_.textures[name].GetAddressOf())))
            || FAILED(device_->CreateSharedHandle(playback_.textures[name].Get(), nullptr, GENERIC_ALL, nullptr, &shared_handle))) {
          *report = std::format("the player's {} ({} x {}, format {}) could not be made", name, texture.width, texture.height, texture.format);
          for (const HANDLE made : opened) CloseHandle(made);
          return false;
        }
        opened.push_back(shared_handle);
        playback_.handles[handle - PLAYER_HANDLES.begin()] = reinterpret_cast<uint64_t>(shared_handle);
      }
    }
    playback_.requests.resize(requests.size());
    playback_.requests.front() = {.path = std::get<2>(requests.front()), .header = first.header, .data = {first.block.begin(), first.block.end()},
                                  .block_bytes = first.block.size()};
    const std::unique_ptr<dlaa_bridge::Shared> first_block = PlaybackBlock(playback_.requests.front(), 0);
    const bool opened_scene = Open(*first_block, playback_.version, true);
    MicromapRebake rebake = {.grass_level = micromap_levels[0], .leaf_level = micromap_levels[1]};
    if (rebake.grass_level >= 0 || rebake.leaf_level >= 0) {
      rebake.block = first_block.get();
      for (const auto& [name, bytes] : first.resources) {
        if (name == "vertices") rebake.vertices = bytes;
        if (name == "atlas") rebake.atlas = bytes;
      }
    } else {
      // The textures made: the first request's bytes let go (the player's memory: WH3's 8 frames are ~10 GB).
      first = {};
    }
    state = {};
    const bool loaded = opened_scene && LoadRecordingState(recording, rebake, &playback_.first_frame, report);
    first = {};
    for (const HANDLE handle : opened) CloseHandle(handle);
    if (!loaded) {
      if (!opened_scene) *report = "the player's textures could not be opened";
      return false;
    }
    playback_.last_frame = std::get<0>(requests.back());
    // Each request's inputs staged once in video memory (an upload's copy there), copied on the GPU into the shared textures
    // before it: a played frame holds the helper's work and a copy (the inputs' uploads took ~200 ms a request at 4K, the
    // GPU idle: Nsight's frames were long and its trace's events ran out).
    for (size_t index = 0; index < requests.size(); ++index) {
      PlaybackFile file;
      const std::filesystem::path& path = std::get<2>(requests[index]);
      if (!ReadPlaybackFile(path, &file, report)) return false;
      ID3D12GraphicsCommandList4* list = RecorderList();
      std::vector<ComPtr<ID3D12Resource>> uploads;
      PlaybackRequest& request = playback_.requests[index];
      request = {.path = path, .header = file.header};
      for (const auto& [name, bytes] : file.resources) {
        const auto texture = playback_.textures.find(name);
        if (texture == playback_.textures.end() || list == nullptr) continue;
        Staged staged;
        if (!StageRecorded(name, texture->second.Get(), bytes, &staged, report)) return false;
        ComPtr<ID3D12Resource> kept = Buffer(staged.buffer->GetDesc().Width, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
        if (!kept) {
          *report = std::format("{}: its {} has no room in video memory", path.filename().string(), name);
          return false;
        }
        list->CopyResource(kept.Get(), staged.buffer.Get());
        uploads.push_back(std::exchange(staged.buffer, kept));
        request.inputs.emplace_back(texture->second.Get(), std::move(staged));
      }
      if (list == nullptr || !WaitFor(SubmitRecorderList(list))) {
        *report = std::format("{}: its inputs could not be uploaded", path.filename().string());
        return false;
      }
      // Its inputs in video memory: the request keeps its block's and cluster sets' bytes alone.
      request.data.assign(file.block.begin(), file.block.end());
      request.data.insert(request.data.end(), file.sets.begin(), file.sets.end());
      request.block_bytes = file.block.size();
      progress(static_cast<uint32_t>((index + 1) * 100 / requests.size()));
    }
    return true;
  }

  // The loaded playback's request `index` (LoadPlayback): its inputs copied into the shared textures (`sun`: the sun
  // texture's first texel, toward the sun, replaced after), its block as recorded but its frame `frame_offset` later
  // (`edit` changes it), then its trace (`before_trace` first: Nsight's frame boundary) or its Light Bounce or reflections.
  // False: a copy failed or the device was lost (`report` says).
  bool PlayRequest(size_t index, uint32_t frame_offset, const std::function<void(dlaa_bridge::Shared*)>& edit, const std::array<float, 4>* sun,
                   const std::function<void()>& before_trace, std::string* report) {
    const PlaybackRequest& request = playback_.requests[index];
    ID3D12GraphicsCommandList4* list = RecorderList();
    ComPtr<ID3D12Resource> sun_upload;
    const auto sun_texture = playback_.textures.find("sun");
    if (list != nullptr) {
      ServeStamp(list, SERVE_INPUTS);
      for (const auto& [texture, staged] : request.inputs) CopyStaged(list, staged, texture, D3D12_RESOURCE_STATE_COMMON);
      if (sun != nullptr && sun_texture != playback_.textures.end()) {
        sun_upload = Buffer(256, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
        void* mapped = nullptr;
        if (!sun_upload || sun_texture->second->GetDesc().Format != DXGI_FORMAT_R32G32B32A32_FLOAT || FAILED(sun_upload->Map(0, nullptr, &mapped))) {
          *report = "the sun's texel could not be written (not RGBA32F, or no memory)";
          return false;
        }
        std::memcpy(mapped, sun->data(), sizeof(*sun));
        sun_upload->Unmap(0, nullptr);
        const auto to_copy = Transition(sun_texture->second.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        list->ResourceBarrier(1, &to_copy);
        const D3D12_TEXTURE_COPY_LOCATION destination = {.pResource = sun_texture->second.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
        const D3D12_TEXTURE_COPY_LOCATION source = {
            .pResource = sun_upload.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
            .PlacedFootprint = {.Footprint = {.Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .Width = 1, .Height = 1, .Depth = 1, .RowPitch = 256}}};
        list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        const auto copied = Transition(sun_texture->second.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        list->ResourceBarrier(1, &copied);
      }
      ServeStamp(list, SERVE_END);
    }
    if (list == nullptr || !WaitFor(SubmitRecorderList(list))) {
      *report = std::format("{}: its inputs could not be copied", request.path.filename().string());
      return false;
    }
    const std::unique_ptr<dlaa_bridge::Shared> block = PlaybackBlock(request, frame_offset);
    edit(block.get());
    // An edited newer field lifted the block's version (rt_serve.h's SHARED_FIELDS): read as that version.
    const uint32_t version = std::max(playback_.version, block->version);
    if (request.header.request == 1) {
      before_trace();
      Trace(*block, version, std::span<const uint8_t>(request.data).subspan(request.block_bytes));
    } else {
      Paths(*block, version, request.header.request == 3);
    }
    if (Lost()) {
      *report = std::format("the device was lost at {}", request.path.filename().string());
      return false;
    }
    return true;
  }

  // The loaded playback's first and last recorded frames, its requests (in frame order), their headers (frame, kind) and
  // its colour texture's size and the last request's active size (its block's rt_width x rt_height).
  [[nodiscard]] std::array<uint32_t, 2> PlaybackFrames() const { return {playback_.first_frame, playback_.last_frame}; }
  [[nodiscard]] size_t PlaybackRequests() const { return playback_.requests.size(); }
  [[nodiscard]] const recording::FileHeader& PlaybackHeader(size_t index) const { return playback_.requests[index].header; }
  [[nodiscard]] std::array<uint32_t, 4> PlaybackSize() const {
    const auto color = playback_.textures.find("color");
    if (color == playback_.textures.end() || playback_.requests.empty()) return {};
    const std::unique_ptr<dlaa_bridge::Shared> block = PlaybackBlock(playback_.requests.back(), 0);
    return {static_cast<uint32_t>(color->second->GetDesc().Width), color->second->GetDesc().Height, block->rt_width, block->rt_height};
  }
  // The made outputs' sizes (ResourceRows' outputs).
  [[nodiscard]] std::map<std::string, std::array<uint32_t, 2>> OutputSizes() {
    std::map<std::string, std::array<uint32_t, 2>> sizes;
    for (const ResourceRow& row : ResourceRows()) {
      if (const ComPtr<ID3D12Resource>* made = row.member(this); row.output && *made) {
        sizes[row.name] = {static_cast<uint32_t>((*made)->GetDesc().Width), (*made)->GetDesc().Height};
      }
    }
    return sizes;
  }

  // The player's "play" (main.cpp): LoadPlayback, then each request (`before_trace` before each trace) with its inputs as
  // recorded, its outputs and each trace's digest into `dump` as the recorder writes them. False: `report` says why.
  bool PlayRecording(const std::filesystem::path& recording, const std::filesystem::path& dump, const std::function<void()>& before_trace,
                     std::string* report) {
    // RENODX_TW_PLAY_MICROMAP_LEVEL / RENODX_TW_PLAY_LEAF_MICROMAP_LEVEL=<level> (tests): grass's / leaves' micromaps baked
    // again at that level from the recorded inputs (MicromapRebake), the rest of the recording as it was.
    const auto level = [](const wchar_t* variable) {
      std::array<wchar_t, 16> value = {};
      return (GetEnvironmentVariableW(variable, value.data(), static_cast<DWORD>(value.size())) != 0) ? std::clamp(_wtoi(value.data()), 0, 12) : -1;
    };
    if (!LoadPlayback(recording, {level(L"RENODX_TW_PLAY_MICROMAP_LEVEL"), level(L"RENODX_TW_PLAY_LEAF_MICROMAP_LEVEL")}, [](uint32_t) {}, report)) {
      return false;
    }
    const std::string loaded_report = *report;
    const auto [first_frame, last_frame] = PlaybackFrames();
    RecordFrames(dump, first_frame, last_frame, false, true);
    // RENODX_TW_PLAY_TRACE_FLAGS / RENODX_TW_PLAY_PATH_FLAGS=<set>[,<clear>] (tests; 0x for hex): every request's rt_flags /
    // rt_path_flags with `clear`'s bits off, then `set`'s on, as the addon's settings would send them (the helper makes
    // what they need as it does live).
    const auto masks = [](const wchar_t* variable) {
      std::array<wchar_t, 64> value = {};
      if (GetEnvironmentVariableW(variable, value.data(), static_cast<DWORD>(value.size())) == 0) return std::pair{0u, 0u};
      wchar_t* rest = nullptr;
      const auto set = static_cast<uint32_t>(std::wcstoul(value.data(), &rest, 0));
      return std::pair{set, (*rest == L',') ? static_cast<uint32_t>(std::wcstoul(rest + 1, nullptr, 0)) : 0u};
    };
    const std::pair<uint32_t, uint32_t> trace_masks = masks(L"RENODX_TW_PLAY_TRACE_FLAGS");
    const std::pair<uint32_t, uint32_t> path_masks = masks(L"RENODX_TW_PLAY_PATH_FLAGS");
    // RENODX_TW_PLAY_LOOPS=<n> (Nsight Graphics' GPU trace): the requests n times over, each pass's frames after the last
    // pass's (the state loaded once: later passes are the same work, not the same pixels; only the first is dumped).
    std::array<wchar_t, 16> loops_value = {};
    const uint32_t loops = std::max<uint32_t>(
        (GetEnvironmentVariableW(L"RENODX_TW_PLAY_LOOPS", loops_value.data(), static_cast<DWORD>(loops_value.size())) != 0) ? _wtoi(loops_value.data()) : 1, 1);
    // RENODX_TW_PLAY_HOLD=<n> (route B references: reference_suite.md 2.3.1, holding the frame): after the passes, the last
    // frame's requests n times more as recorded, a frame later each (not dumped). The helper's per-frame state then follows
    // from consecutive frames of one block as it does live: its parities turn, its history-valid bits are on (each
    // history's frame the one before), light_previous is the identity (the same lights) and the previous positions and
    // view-projection are the current ones (the same inputs), so the scene digest stays the held frame's.
    std::array<wchar_t, 16> hold_value = {};
    const uint32_t hold = static_cast<uint32_t>(std::max(
        (GetEnvironmentVariableW(L"RENODX_TW_PLAY_HOLD", hold_value.data(), static_cast<DWORD>(hold_value.size())) != 0) ? _wtoi(hold_value.data()) : 0, 0));
    const uint32_t pass_frames = last_frame - first_frame + 1;
    const size_t last_frame_request = std::ranges::find(playback_.requests, last_frame, [](const PlaybackRequest& request) { return request.header.frame; })
                                      - playback_.requests.begin();
    uint32_t played = 0;
    std::string flags;  // the masked requests' flags as recorded and as played
    for (uint32_t loop = 0; loop < loops + hold; ++loop) {
      const bool held = loop >= loops;
      const uint32_t frame_offset = (held ? ((loops - 1) * pass_frames) + (loop - loops + 1) : (loop * pass_frames));
      for (size_t index = (held ? last_frame_request : 0); index < PlaybackRequests(); ++index) {
        const auto edit = [&](dlaa_bridge::Shared* block) {
          const uint32_t recorded_flags = std::exchange(block->rt_flags, (block->rt_flags & ~trace_masks.second) | trace_masks.first);
          const uint32_t recorded_path_flags = std::exchange(block->rt_path_flags, (block->rt_path_flags & ~path_masks.second) | path_masks.first);
          if (loop == 0 && (trace_masks.first | trace_masks.second | path_masks.first | path_masks.second) != 0) {
            flags += std::format("; {}: flags {} -> {}, path flags {} -> {}", playback_.requests[index].path.filename().string(), recorded_flags,
                                 block->rt_flags, recorded_path_flags, block->rt_path_flags);
          }
        };
        if (!PlayRequest(index, frame_offset, edit, nullptr, before_trace, report)) {
          *report += std::format(" (pass {})", loop + 1);
          return false;
        }
        ++played;
      }
    }
    *report = std::format("{}; {} requests played ({} passes{}){}: {}", loaded_report, played, loops,
                          (hold != 0) ? std::format(", the last frame held {} times", hold) : std::string(), flags, FinishRecording());
    return true;
  }

  // The resident's scene primitives (rt_serve.h: its snapshot and resets, its timing, burn, stems and moments).
  //
  // Every RecordedFields() field's bytes by its name, in their order.
  std::vector<std::pair<std::string_view, std::span<std::byte>>> FieldSpans() {
    std::vector<std::pair<std::string_view, std::span<std::byte>>> spans;
    std::apply([&](const auto&... field) { (spans.emplace_back(field.first, std::as_writable_bytes(std::span(&(this->*field.second), 1))), ...); },
               RecordedFields());
    return spans;
  }

  // A resource in a resident's snapshot (SaveResources): a copy in video memory, or its read-back's bytes as a recording's
  // chunk holds them.
  struct SavedResource {
    std::string name;
    ID3D12Resource* resource = nullptr;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    ComPtr<ID3D12Resource> copy;
    std::vector<uint8_t> bytes;
  };
  // The made ResourceRows of `names` and every NRD denoiser's history (between requests, resting in their states) copied
  // into `saved`: a copy each in video memory with `vram`, else read back; `*bytes` their size. False: `why` says why.
  bool SaveResources(const std::vector<std::string_view>& names, bool vram, std::vector<SavedResource>* saved, uint64_t* bytes, std::string* why) {
    saved->clear();
    *bytes = 0;
    for (const RecordedResource& resource : RecordedResources()) {
      if (*resource.member && std::ranges::find(names, std::string_view(resource.name)) != names.end()) {
        saved->push_back({.name = resource.name, .resource = resource.member->Get(), .state = resource.state});
      }
    }
#if defined(RENODX_NRD)
    for (size_t signal = 0; signal < NrdSignals().size(); ++signal) {
      if (!NrdSignals()[signal]->denoiser) continue;
      const auto history = NrdSignals()[signal]->denoiser->History();
      for (size_t index = 0; index < history.size(); ++index) {
        saved->push_back({.name = std::format("nrd{}_{}", signal, index), .resource = history[index].first, .state = history[index].second});
      }
    }
#endif
    ID3D12GraphicsCommandList4* list = RecorderList();
    if (list == nullptr || !WaitFor(own_value_)) {
      *why = "no command list for the snapshot";
      return false;
    }
    std::vector<Readback> readbacks;
    for (SavedResource& resource : *saved) {
      D3D12_RESOURCE_DESC desc = resource.resource->GetDesc();
      *bytes += device_->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
      if (!vram) {
        CopyToReadback(list, resource.name.c_str(), resource.resource, resource.state, &readbacks);
        continue;
      }
      const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
      if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                  IID_PPV_ARGS(resource.copy.GetAddressOf())))) {
        *why = std::format("no video memory for {}'s copy", resource.name);
        SubmitRecorderList(list);
        return false;
      }
      const auto to_copy = Transition(resource.resource, resource.state, D3D12_RESOURCE_STATE_COPY_SOURCE);
      list->ResourceBarrier(1, &to_copy);
      list->CopyResource(resource.copy.Get(), resource.resource);
      const std::array<D3D12_RESOURCE_BARRIER, 2> copied = {Transition(resource.resource, D3D12_RESOURCE_STATE_COPY_SOURCE, resource.state),
                                                            Transition(resource.copy.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE)};
      list->ResourceBarrier(2, copied.data());
    }
    if (!WaitFor(SubmitRecorderList(list)) || (!vram && readbacks.size() != saved->size())) {
      *why = vram ? "the snapshot's copies failed" : "the snapshot's read-backs failed (no memory)";
      return false;
    }
    for (size_t index = 0; index < readbacks.size(); ++index) {
      if (!ReadbackPayload(&readbacks[index], &(*saved)[index].bytes)) {
        *why = std::format("{} could not be read back", readbacks[index].name);
        return false;
      }
    }
    return true;
  }
  // rt_place's frame stamps in placed instance descs (the low 24 bits of each one's word at +52) `shift` frames later, as a
  // frame played `shift` frames later writes them.
  static void ShiftPlacedStamps(std::span<uint8_t> descs, uint32_t shift) {
    for (size_t at = 52; at + sizeof(uint32_t) <= descs.size(); at += sizeof(D3D12_RAYTRACING_INSTANCE_DESC)) {
      uint32_t word = 0;
      std::memcpy(&word, descs.data() + at, sizeof(word));
      word = ((word + shift) & 0xFFFFFFu) | (word & 0xFF000000u);
      std::memcpy(descs.data() + at, &word, sizeof(word));
    }
  }
  // The radiance cache's frame stamps (rt_cache_resolve: frame + 1 in each cell's radiance w, a float, and albedo y; 0:
  // none) `shift` frames later. Exact while frames stay below 2^24 (the resident's seed limit).
  static void ShiftCacheStamps(std::span<uint8_t> radiance, std::span<uint8_t> albedo, uint32_t shift) {
    for (size_t at = 12; at + sizeof(float) <= radiance.size(); at += 16) {
      float stamp = 0.f;
      std::memcpy(&stamp, radiance.data() + at, sizeof(stamp));
      if (stamp > 0.f) stamp = static_cast<float>(static_cast<uint32_t>(stamp) + shift);
      std::memcpy(radiance.data() + at, &stamp, sizeof(stamp));
    }
    for (size_t at = 4; at + sizeof(uint32_t) <= albedo.size(); at += 8) {
      uint32_t stamp = 0;
      std::memcpy(&stamp, albedo.data() + at, sizeof(stamp));
      if (stamp != 0) stamp += shift;
      std::memcpy(albedo.data() + at, &stamp, sizeof(stamp));
    }
  }
  // `saved` (SaveResources' on this scene, its resources unchanged since: Structure) copied back, the GPU's frame stamps
  // `shift` frames later: the placed instances' kept descs (from the placed state's CPU descs' end to the last trace's
  // count) and the radiance cache's cells (read back, shifted, written again). False: `why` says why.
  bool RestoreResources(const std::vector<SavedResource>& saved, uint32_t shift, std::string* why) {
    ID3D12GraphicsCommandList4* list = RecorderList();
    std::vector<ComPtr<ID3D12Resource>> uploads;
    for (const SavedResource& resource : saved) {
      if (list == nullptr) break;
      if (!resource.copy) {
        if (!UploadRecorded(list, resource.name, resource.resource, resource.state, resource.bytes, &uploads, why)) return false;
        continue;
      }
      const auto to_copy = Transition(resource.resource, resource.state, D3D12_RESOURCE_STATE_COPY_DEST);
      list->ResourceBarrier(1, &to_copy);
      list->CopyResource(resource.resource, resource.copy.Get());
      const auto copied = Transition(resource.resource, D3D12_RESOURCE_STATE_COPY_DEST, resource.state);
      list->ResourceBarrier(1, &copied);
    }
    if (list == nullptr || !WaitFor(SubmitRecorderList(list))) {
      *why = "the reset's copies failed";
      return false;
    }
    if (shift == 0) return true;
    // The stamped buffers' parts read back: the placed state's first 16 bytes and the kept descs, the cache's two pools.
    struct Stamped {
      const char* name;
      ID3D12Resource* resource;
      D3D12_RESOURCE_STATES state;
      uint64_t bytes;  // 0: all of it
    };
    const uint64_t descs = uint64_t{std::min(placed_written_, placed_capacity_)} * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
    const bool placed = placed_state_ && descs != 0;
    const bool cached = cache_radiance_ && cache_albedo_;
    std::vector<Stamped> stamped;
    if (placed) {
      stamped.push_back({"placed_state", placed_state_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 16});
      stamped.push_back({"placed", placed_instances_[placed_last_].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, descs});
    }
    if (cached) {
      stamped.push_back({"cache_radiance", cache_radiance_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0});
      stamped.push_back({"cache_albedo", cache_albedo_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0});
    }
    if (stamped.empty()) return true;
    std::vector<Readback> readbacks;
    std::vector<std::vector<uint8_t>> payloads(stamped.size());
    list = RecorderList();
    for (const Stamped& buffer : stamped) {
      if (list != nullptr) CopyToReadback(list, buffer.name, buffer.resource, buffer.state, &readbacks, 0, buffer.bytes);
    }
    bool read = list != nullptr && WaitFor(SubmitRecorderList(list)) && readbacks.size() == stamped.size();
    for (size_t index = 0; read && index < stamped.size(); ++index) read = ReadbackPayload(&readbacks[index], &payloads[index]);
    if (!read) {
      *why = "the GPU's frame stamps could not be read back";
      return false;
    }
    const auto body = [](std::vector<uint8_t>* payload) { return std::span(*payload).subspan(sizeof(recording::BufferHeader)); };
    if (placed) {
      uint32_t cpu_descs = 0;  // rt_place's STATE_CPU
      std::memcpy(&cpu_descs, body(&payloads[0]).data() + 12, sizeof(cpu_descs));
      ShiftPlacedStamps(body(&payloads[1]).subspan(std::min(uint64_t{cpu_descs} * sizeof(D3D12_RAYTRACING_INSTANCE_DESC), descs)), shift);
    }
    if (cached) ShiftCacheStamps(body(&payloads[payloads.size() - 2]), body(&payloads.back()), shift);
    list = RecorderList();
    bool written = list != nullptr;
    for (size_t index = placed ? 1 : 0; written && index < stamped.size(); ++index) {
      written = UploadRecorded(list, stamped[index].name, stamped[index].resource, stamped[index].state, payloads[index], &uploads, why);
    }
    // An upload's own reason kept; else the list's.
    const bool finished = list != nullptr && WaitFor(SubmitRecorderList(list));
    if (!written || !finished) {
      if (written || list == nullptr) *why = "the GPU's frame stamps could not be written back";
      return false;
    }
    return true;
  }

  // What a reset must find as the snapshot left it (order-free): the static meshes and cluster sets, the GPU resources
  // (each one's place and size), the pools' free ranges and pending frees, the NRD denoisers.
  uint64_t Structure() {
    std::vector<uint64_t> parts;
    const auto add = [&](const auto& value) { parts.push_back(HashBytes(std::as_bytes(std::span(&value, 1)))); };
    for (const auto& [key, mesh] : meshes_) {
      const std::array<uint64_t, 6> fields = {key, mesh.build, mesh.first_instance, mesh.record, mesh.triangles,
                                              HashBytes(std::as_bytes(std::span(mesh.transforms)))};
      add(fields);
    }
    for (const auto& [key, set] : sets_) add(std::array<uint64_t, 4>{key, set.generation, set.offset, set.units});
    std::ranges::sort(parts);
    for (const RecordedResource& resource : RecordedResources()) {
      const D3D12_RESOURCE_DESC desc = *resource.member ? resource.member->Get()->GetDesc() : D3D12_RESOURCE_DESC{};
      add(std::array<uint64_t, 3>{reinterpret_cast<uintptr_t>(resource.member->Get()), desc.Width, desc.Height});
    }
    for (const Ranges* ranges : {&free_triangles_, &free_uvs_, &free_instances_, &free_set_units_, &free_templates_}) {
      for (const auto& [offset, count] : ranges->free) add(std::array<uint32_t, 2>{offset, count});
    }
    parts.push_back(HashBytes(std::as_bytes(std::span(free_records_))));
    for (const PendingFree& pending : pending_frees_) add(std::array<uint32_t, 3>{pending.record, pending.normal_offset, pending.triangles});
    for (const PendingSetFree& pending : pending_set_frees_) add(std::array<uint32_t, 2>{pending.offset, pending.units});
#if defined(RENODX_NRD)
    for (const NrdSignal* nrd : NrdSignals()) {
      add(nrd->size);
      if (nrd->denoiser) {
        for (const auto& [texture, state] : nrd->denoiser->History()) add(reinterpret_cast<uintptr_t>(texture));
      }
    }
#endif
    return HashBytes(std::as_bytes(std::span(parts)));
  }

  // The CPU state a reset gives back beside the fields: each static mesh's last drawn frame and moved flag, each NRD
  // denoiser's last frame's fields and use.
  struct Objects {
    std::vector<std::tuple<uint64_t, uint32_t, bool>> meshes;
#if defined(RENODX_NRD)
    std::array<std::optional<std::pair<Nrd12::Kept, uint32_t>>, 4> nrd;
#endif
  };
  // Objects of this scene between requests; the last trace's placed instances' counter taken (as SaveRecordingState takes
  // it) and the units' BLAS built anew at the next trace (a refit is not the build), here and at each reset.
  Objects SaveObjects() {
    if (placed_slot_ != SLOTS) {
      placed_written_ = slots_[placed_slot_].left_out_data[LEFT_OUT_BYTES / 4];
      placed_slot_ = SLOTS;
    }
    units_layout_.clear();
    units_refits_ = 0;
    Objects objects;
    for (const auto& [key, mesh] : meshes_) objects.meshes.emplace_back(key, mesh.drawn_frame, mesh.moved);
#if defined(RENODX_NRD)
    for (size_t signal = 0; signal < NrdSignals().size(); ++signal) {
      const NrdSignal& nrd = *NrdSignals()[signal];
      if (nrd.denoiser) objects.nrd[signal] = std::pair{nrd.denoiser->Saved(), nrd.used_frame};
    }
#endif
    return objects;
  }
  // `objects` given back (Structure unchanged), their frames `shift` later (UINT32_MAX: none, kept).
  void RestoreObjects(const Objects& objects, uint32_t shift) {
    const auto shifted = [&](uint32_t frame) { return (frame == UINT32_MAX) ? frame : frame + shift; };
    placed_slot_ = SLOTS;
    units_layout_.clear();
    units_refits_ = 0;
    for (const auto& [key, drawn_frame, moved] : objects.meshes) {
      Mesh& mesh = meshes_.at(key);
      mesh.drawn_frame = shifted(drawn_frame);
      mesh.moved = moved;
    }
#if defined(RENODX_NRD)
    for (size_t signal = 0; signal < NrdSignals().size(); ++signal) {
      NrdSignal& nrd = *NrdSignals()[signal];
      if (!nrd.denoiser || !objects.nrd[signal]) continue;
      Nrd12::Kept kept = objects.nrd[signal]->first;
      kept.last_frame = shifted(kept.last_frame);
      nrd.denoiser->Restore(kept);
      nrd.used_frame = shifted(objects.nrd[signal]->second);
    }
#endif
  }

  // True: nothing on its way that a snapshot cannot hold (micromap bakes, instanced rows read back, a room growing); else
  // `why` says what.
  bool Settled(std::string* why) {
    for (Slot& slot : slots_) TakeJournalReads(&slot);
    const size_t bakes = micromap_reads_.size() + micromap_queue_.size() + MicromapsBaking() + micromaps_baked_.size();
    const auto row_reads = std::ranges::count_if(slots_, [](const Slot& slot) { return !slot.row_reads.empty(); });
#if defined(RENODX_NVAPI_CLUSTERS)
    const bool growing = placed_growing_.valid() || cluster_growing_.valid();
#else
    const bool growing = placed_growing_.valid();
#endif
    if (bakes != 0 || row_reads != 0 || growing) {
      *why = std::format("waiting for {} micromap bakes, {} slots' instanced rows{} (no new bakes meanwhile)", bakes, row_reads, growing ? ", a room growing" : "");
      return false;
    }
    return true;
  }

  // The resident's GPU times: off; each list's start and end ("total", "player inputs"); every Mark() pass too.
  enum ServeTimingMode : uint32_t { SERVE_TIMING_OFF, SERVE_TIMING_TOTAL, SERVE_TIMING_PASSES };
  // `mode` from the next request on (its own timestamps; no RENODX_TW_NO_GPU_TIMES needed). False: no query heap here.
  bool ServeTiming(ServeTimingMode mode) {
    serve_timing_ = mode;
    serve_marks_.clear();
    serve_list_ = nullptr;
    if (mode == SERVE_TIMING_OFF || serve_stamps_) return true;
    const D3D12_QUERY_HEAP_DESC heap_desc = {.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP, .Count = SERVE_STAMPS};
    serve_readback_ = Buffer(SERVE_STAMPS * sizeof(uint64_t), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    if (!serve_readback_ || FAILED(device_->CreateQueryHeap(&heap_desc, IID_PPV_ARGS(serve_stamps_.GetAddressOf())))) {
      serve_timing_ = SERVE_TIMING_OFF;
      return false;
    }
    return true;
  }
  // The last request's GPU ms by row (its GPU work done: the player waits for each request): "total" its lists' starts to
  // ends, "player inputs" its inputs' copy, with SERVE_TIMING_PASSES each Mark() pass to the next ("unmarked": before the
  // first). A list that was not run (no end) adds nothing.
  std::vector<std::pair<std::string, double>> ReadServeTimes() {
    std::vector<std::pair<std::string, double>> rows;
    const auto add = [](std::vector<std::pair<std::string, double>>* into, const std::string& row, double ms) {
      const auto found = std::ranges::find(*into, row, &std::pair<std::string, double>::first);
      if (found == into->end()) {
        into->emplace_back(row, ms);
      } else {
        found->second += ms;
      }
    };
    uint64_t* stamps = nullptr;
    if (timestamp_frequency_ == 0) queue_->GetTimestampFrequency(&timestamp_frequency_);
    if (!serve_marks_.empty() && timestamp_frequency_ != 0 && SUCCEEDED(serve_readback_->Map(0, nullptr, reinterpret_cast<void**>(&stamps)))) {
      const auto ms = [&](size_t from, size_t to) {
        return (stamps[to] > stamps[from]) ? static_cast<double>(stamps[to] - stamps[from]) * 1000.0 / static_cast<double>(timestamp_frequency_) : 0.0;
      };
      std::vector<std::pair<std::string, double>> list_rows;  // the list's so far, added at its end
      size_t start = SIZE_MAX;
      size_t last = 0;
      std::string current;
      for (size_t index = 0; index < serve_marks_.size(); ++index) {
        const ServeMarkEntry& entry = serve_marks_[index];
        if (entry.kind == SERVE_START || entry.kind == SERVE_INPUTS) {
          list_rows.clear();
          start = index;
          last = index;
          current = "unmarked";
        } else if (start != SIZE_MAX && entry.kind == SERVE_MARK) {
          add(&list_rows, current, ms(last, index));
          current = entry.row;
          last = index;
        } else if (start != SIZE_MAX && entry.kind == SERVE_END) {
          if (serve_marks_[start].kind == SERVE_INPUTS) {
            add(&rows, "player inputs", ms(start, index));
          } else {
            if (serve_timing_ == SERVE_TIMING_PASSES) add(&list_rows, current, ms(last, index));
            for (const auto& [row, row_ms] : list_rows) add(&rows, row, row_ms);
            add(&rows, "total", ms(start, index));
          }
          start = SIZE_MAX;
        }
      }
      const D3D12_RANGE written = {};
      serve_readback_->Unmap(0, &written);
    }
    serve_marks_.clear();
    serve_list_ = nullptr;
    return rows;
  }

  // The burn (rt_burn.hlsl): `loops` dispatches of a fixed chain in `pass`'s span (after its Mark), a pass whose pipeline is
  // set right after its Mark with the scene's root signature bound (the burn's is the same: the bindings stay). 0: none.
  // compact and place run only when a request has their work: BurnRuns counts the passes that burnt. False: `why` says why
  // (another pass, no pipeline).
  bool SetBurn(std::string_view pass, uint32_t loops, std::string* why) {
    static constexpr std::array<std::string_view, 13> BURNABLE = {"trace", "local lights", "fog", "soft", "bounce", "radiance cache", "path tracing",
                                                                  "reuse", "reflection reservoirs", "reflections", "scene data", "compact", "place"};
    burn_loops_ = 0;
    burn_runs_ = 0;
    if (loops == 0) return true;
    if (std::ranges::find(BURNABLE, pass) == BURNABLE.end()) {
      *why = std::format("the burn cannot run in pass \"{}\" (only: trace, local lights, fog, soft, bounce, radiance cache, path tracing, reuse, "
                         "reflection reservoirs, reflections, scene data, compact, place)",
                         pass);
      return false;
    }
    const D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {.pRootSignature = root_signature_.Get(), .CS = {.pShaderBytecode = RT_BURN, .BytecodeLength = sizeof(RT_BURN)}};
    if (!burn_pipeline_ && FAILED(device_->CreateComputePipelineState(&desc, IID_PPV_ARGS(burn_pipeline_.GetAddressOf())))) {
      *why = "the burn's pipeline failed";
      return false;
    }
    KeepCode(burn_pipeline_.Get(), RT_BURN, sizeof(RT_BURN));
    burn_pass_.assign(pass.begin(), pass.end());
    burn_loops_ = loops;
    return true;
  }
  [[nodiscard]] uint32_t BurnRuns() const { return burn_runs_; }

  // A stem's pipeline from `code` (a compute shader's DXIL, checked by rt_serve.h; empty: the base build's again), made with
  // the scene's root signature: rt_trace, rt_bounce, rt_path, rt_reflect, rt_reuse, rt_reflect_reservoir, rt_fog, rt_lights
  // (their DXR 1.2 builds where Dxr12()), rt_soft, rt_scene_data, rt_place, rt_cache_fill, rt_cache_resolve. False: `why`.
  // SwapStem's stems and their pipelines (the first eight: DXR 1.2 builds where Dxr12(), their headers' _omm).
  using StemPipeline = std::pair<std::string_view, ComPtr<ID3D12PipelineState> RtScene12::*>;
  static constexpr size_t OMM_STEMS = 8;
  static constexpr std::array<StemPipeline, 13> SwapStems() {
    return {{
        {"rt_trace", &RtScene12::trace_pipeline_},
        {"rt_bounce", &RtScene12::bounce_pipeline_},
        {"rt_path", &RtScene12::path_pipeline_},
        {"rt_reflect", &RtScene12::reflect_pipeline_},
        {"rt_reuse", &RtScene12::reuse_pipeline_},
        {"rt_reflect_reservoir", &RtScene12::reflect_reservoir_pipeline_},
        {"rt_fog", &RtScene12::fog_pipeline_},
        {"rt_lights", &RtScene12::lights_pipeline_},
        {"rt_soft", &RtScene12::soft_pipeline_},
        {"rt_scene_data", &RtScene12::scene_data_pipeline_},
        {"rt_place", &RtScene12::place_pipeline_},
        {"rt_cache_fill", &RtScene12::cache_fill_pipeline_},
        {"rt_cache_resolve", &RtScene12::cache_resolve_pipeline_},
    }};
  }
  bool SwapStem(std::string_view stem, std::span<const uint8_t> code, std::string* why) {
    static constexpr std::array<StemPipeline, 13> STEMS = SwapStems();
    const auto found = std::ranges::find(STEMS, stem, &StemPipeline::first);
    if (found == STEMS.end()) {
      *why = std::format("unknown stem {}", stem);
      return false;
    }
    // While Torch Light in PT GI's T1 is held (asked within HISTORY_FRAMES), rt_lights' twins run in its place: a swap of
    // it would measure the twin on both sides. Lab jobs that swap rt_lights keep bit 27 clear (the twins are no stems).
    if (stem == "rt_lights" && !code.empty() && lights_torch_pipelines_[0] && torch_irradiance_) {
      *why = "rt_lights' twins (rt_lights_torch) run in its place while Torch Light in PT GI (bit 27) is on";
      return false;
    }
    ComPtr<ID3D12PipelineState>& pipeline = this->*found->second;
    const auto base = base_pipelines_.try_emplace(std::string(stem), pipeline).first;
    if (code.empty()) {
      pipeline = base->second;
      return true;
    }
    ComPtr<ID3D12PipelineState> made;
    const D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {.pRootSignature = root_signature_.Get(), .CS = {.pShaderBytecode = code.data(), .BytecodeLength = code.size()}};
    if (FAILED(device_->CreateComputePipelineState(&desc, IID_PPV_ARGS(made.GetAddressOf())))) {
      *why = std::format("{}'s pipeline could not be made from its DXIL (another root signature or shader model?)", stem);
      return false;
    }
    KeepCode(made.Get(), code.data(), code.size());
    pipeline = made;
    return true;
  }

  // The resource-use log (rt_uses.h; rt_serve.h's `run` with log_uses): the slots' lists' work from now on, each frame's
  // rows into `folder`/uses.tsv, each stem's DXIL beside it as <stem>.dxil. False: `why` says why.
  bool StartUses(const std::filesystem::path& folder, std::string* why) {
    use_file_.open(folder / "uses.tsv", std::ios::trunc);
    if (!use_file_) {
      *why = std::format("{} could not be written", (folder / "uses.tsv").string());
      return false;
    }
    use_file_ << "# renodx resident resource-use log v1 (tools/tw/round_notes/plan_1117/designs/grs_uses_log.md)\n"
              << "# frame\tspan\tstem\tkind\tslot\tresource\n";
    use_folder_ = folder;
    use_written_.clear();
    use_unnamed_.clear();
    std::vector<ID3D12GraphicsCommandList4*> lists;
    for (const Slot& slot : slots_) lists.push_back(slot.list.Get());
    uses_ = std::make_unique<uses::Log<RtScene12>>(this);
    if (!uses_->Hook(lists, why)) {
      uses_.reset();
      use_file_.close();
      return false;
    }
    return true;
  }
  // The rows so far written; the next ones frame `frame`'s.
  void UsesFrame(uint32_t frame) {
    if (!uses_) return;
    use_file_ << uses_->TakeRows();
    uses_->Frame(frame);
  }
  // The log off, its last rows and the external resources written: the shared outputs (the game reads them, dump writes
  // them), NRD's inputs (NRD's own root signature reads them), what NVAPI's cluster and partitioned TLAS operations read.
  // False: no log, or uses.tsv not written.
  bool StopUses() {
    if (!uses_) return false;
    for (const ResourceRow& row : ResourceRows()) {
      if (row.output) {
        uses_->External(row.name);
      }
    }
    for (const UseName& entry : UseNames()) {
      for (UINT set = 0; (entry.member == nullptr || entry.nvapi) && set < entry.sets; ++set) {
        uses_->External(UseSetName(entry, set));
      }
    }
    for (size_t index = 0; index < slots_.size(); ++index) {
      uses_->External(std::format("slot{}_ptlas_device", index));
    }
    use_file_ << uses_->TakeRows() << uses_->ExternalRows();
    uses_.reset();
    use_file_.close();
    return !use_file_.fail();
  }
  // rt_uses.h's names: the scene's root signature; a pipeline's stem (SwapStem's by its build: <stem>[_omm], a swapped one's
  // .<digest>; others dxil.<digest>, the DXIL container's digest; unknown_pipeline: not made here), its DXIL written once.
  bool UseOwnRoot(ID3D12RootSignature* root) const { return root != nullptr && root == root_signature_.Get(); }
  std::string UseStem(ID3D12PipelineState* pipeline) {
    const auto found = use_code_.find(pipeline);
    if (found == use_code_.end() || found->second.size() < 20) return "unknown_pipeline";
    const std::vector<uint8_t>& code = found->second;
    std::string digest;
    for (size_t at = 4; at < 10; ++at) digest += std::format("{:02x}", code[at]);
    std::string name = "dxil." + digest;
    static constexpr std::array<StemPipeline, 13> STEMS = SwapStems();
    for (size_t index = 0; index < STEMS.size(); ++index) {
      const auto& [stem, member] = STEMS[index];
      if ((this->*member).Get() != pipeline) continue;
      const auto base = base_pipelines_.find(std::string(stem));
      name = std::format("{}{}{}", stem, (index < OMM_STEMS && dxr12_) ? "_omm" : "",
                         (base != base_pipelines_.end() && base->second.Get() != pipeline) ? "." + digest : "");
    }
    if (!use_written_.contains(name)
        && std::ofstream(use_folder_ / (name + ".dxil"), std::ios::binary | std::ios::trunc)
               .write(reinterpret_cast<const char*>(code.data()), static_cast<std::streamsize>(code.size()))) {
      use_written_.insert(name);
    }
    return name;
  }
  // The live table's views by UseNames (another table or heap: none): "uav" / "srv", the register, the resource (none
  // for a null view; NRD's textures by their descriptors' names).
  template <typename Emit>
  void UseTable(D3D12_GPU_DESCRIPTOR_HANDLE table, const Emit& emit) {
    if (table.ptr != descriptors_->GetGPUDescriptorHandleForHeapStart().ptr) return;
    for (const UseName& entry : UseNames()) {
      for (UINT set = 0; entry.descriptor != NO_DESCRIPTOR && set < entry.sets; ++set) {
        const UINT descriptor = entry.descriptor + set;
        const auto range = std::ranges::find_if(use_ranges_, [&](const D3D12_DESCRIPTOR_RANGE& kind) {
          return descriptor >= kind.OffsetInDescriptorsFromTableStart && descriptor < kind.OffsetInDescriptorsFromTableStart + kind.NumDescriptors;
        });
        ID3D12Resource* resource = (entry.member != nullptr) ? entry.member(this)[set].Get() : nullptr;
        if (range == use_ranges_.end() || (entry.member != nullptr && resource == nullptr)) continue;
        const bool uav = range->RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        emit(uav ? "uav" : "srv",
             std::format("{}{}:{}", uav ? 'u' : 't', range->BaseShaderRegister + descriptor - range->OffsetInDescriptorsFromTableStart, range->RegisterSpace),
             (resource != nullptr) ? UseResource(resource) : std::string(entry.name));
      }
    }
  }
  // A root SRV's or UAV's register ("u6:0").
  std::string UseRootSlot(UINT parameter) const {
    if (parameter >= use_parameters_.size()) return "?";
    const D3D12_ROOT_PARAMETER& root = use_parameters_[parameter];
    return std::format("{}{}:{}", (root.ParameterType == D3D12_ROOT_PARAMETER_TYPE_UAV) ? 'u' : 't', root.Descriptor.ShaderRegister, root.Descriptor.RegisterSpace);
  }
  // A resource's name: its ResourceRows or UseNames name, a slot's buffer's slot<n>_<field>, else buffer#<address> /
  // texture#<address> (a buffer by GPU address: the one holding it, an unnamed one seen by pointer before, else va:<address>).
  std::string UseResource(ID3D12Resource* resource) {
    if (resource == nullptr) return "null";
    if (std::string name = UseFind([&](ID3D12Resource* candidate) { return candidate == resource; }); !name.empty()) return name;
    const D3D12_RESOURCE_DESC desc = resource->GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) return std::format("texture#{:x}", reinterpret_cast<uintptr_t>(resource));
    std::string name = std::format("buffer#{:x}", reinterpret_cast<uintptr_t>(resource));
    use_unnamed_.insert_or_assign(resource->GetGPUVirtualAddress(), std::pair{desc.Width, name});
    return name;
  }
  std::string UseAddress(D3D12_GPU_VIRTUAL_ADDRESS address) {
    if (std::string name = UseFind([&](ID3D12Resource* candidate) {
          return candidate != nullptr && candidate->GetDesc().Dimension == D3D12_RESOURCE_DIMENSION_BUFFER && address >= candidate->GetGPUVirtualAddress()
                 && address < candidate->GetGPUVirtualAddress() + candidate->GetDesc().Width;
        });
        !name.empty()) {
      return name;
    }
    if (const auto after = use_unnamed_.upper_bound(address); after != use_unnamed_.begin() && address < std::prev(after)->first + std::prev(after)->second.first) {
      return std::prev(after)->second.second;
    }
    return std::format("va:{:x}", address);
  }
  static bool UseReadBack(ID3D12Resource* resource) {
    D3D12_HEAP_PROPERTIES heap = {};
    return resource != nullptr && SUCCEEDED(resource->GetHeapProperties(&heap, nullptr)) && heap.Type == D3D12_HEAP_TYPE_READBACK;
  }
  [[nodiscard]] ID3D12Fence* OwnFence() const { return own_fence_.Get(); }

  // B2's moments (rt_moments.hlsl): per texel of each named output's first subresource, fp32 Welford planes (MOMENT_PLANES:
  // the mean about the first frame, M2, the first frame, the lag-1 co-moment about it, the last frame, the sum of |change|), one AddMoments a frame.
  // StartMoments refuses an integer output, an unknown one, or more than `headroom` / 1.3 bytes.
  static constexpr uint32_t MOMENT_PLANES = 6;
  // The moments let go (a failed command's).
  void StopMoments() { moments_ = {}; }
  // A moments output (StartMoments): its texture and its planes.
  struct MomentOutput {
    std::string name;
    ID3D12Resource* source = nullptr;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    uint32_t width = 0;
    uint32_t height = 0;
    std::array<ComPtr<ID3D12Resource>, MOMENT_PLANES> planes;
  };
  bool StartMoments(const std::vector<std::string>& outputs, uint64_t headroom, std::string* why) {
    static constexpr std::array<DXGI_FORMAT, 21> INTEGERS = {
        DXGI_FORMAT_R32G32B32A32_UINT, DXGI_FORMAT_R32G32B32A32_SINT, DXGI_FORMAT_R32G32B32_UINT, DXGI_FORMAT_R32G32B32_SINT,
        DXGI_FORMAT_R16G16B16A16_UINT, DXGI_FORMAT_R16G16B16A16_SINT, DXGI_FORMAT_R32G32_UINT,    DXGI_FORMAT_R32G32_SINT,
        DXGI_FORMAT_R10G10B10A2_UINT,  DXGI_FORMAT_R8G8B8A8_UINT,     DXGI_FORMAT_R8G8B8A8_SINT,  DXGI_FORMAT_R16G16_UINT,
        DXGI_FORMAT_R16G16_SINT,       DXGI_FORMAT_R32_UINT,          DXGI_FORMAT_R32_SINT,       DXGI_FORMAT_R8G8_UINT,
        DXGI_FORMAT_R8G8_SINT,         DXGI_FORMAT_R16_UINT,          DXGI_FORMAT_R16_SINT,       DXGI_FORMAT_R8_UINT,
        DXGI_FORMAT_R8_SINT};
    moments_ = {};
    uint64_t bytes = 0;
    const auto rows = ResourceRows();
    for (const std::string& name : outputs) {
      const auto row = std::ranges::find_if(rows, [&](const ResourceRow& resource) { return resource.output && name == resource.name; });
      ID3D12Resource* source = (row != rows.end()) ? row->member(this)->Get() : nullptr;
      if (source == nullptr) {
        *why = std::format("no output {} in this scene", name);
        return false;
      }
      const D3D12_RESOURCE_DESC desc = source->GetDesc();
      if (std::ranges::find(INTEGERS, desc.Format) != INTEGERS.end()) {
        *why = std::format("{} is an integer texture (format {}): no moments", name, static_cast<uint32_t>(desc.Format));
        return false;
      }
      moments_.outputs.push_back({.name = name, .source = source, .format = desc.Format, .width = static_cast<uint32_t>(desc.Width), .height = desc.Height});
      bytes += uint64_t{MOMENT_PLANES} * desc.Width * desc.Height * 16;
    }
    if (bytes * 13 / 10 > headroom) {
      *why = std::format("the moments need {} MB, 1.3 x that is past the {} MB of video memory free", bytes >> 20, headroom >> 20);
      moments_ = {};
      return false;
    }
    const std::array<D3D12_DESCRIPTOR_RANGE, 2> ranges = {{
        {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV, .NumDescriptors = 1, .BaseShaderRegister = 0, .OffsetInDescriptorsFromTableStart = 0},
        {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV, .NumDescriptors = MOMENT_PLANES, .BaseShaderRegister = 0, .OffsetInDescriptorsFromTableStart = 1},
    }};
    const std::array<D3D12_ROOT_PARAMETER, 2> parameters = {{
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
         .DescriptorTable = {.NumDescriptorRanges = static_cast<UINT>(ranges.size()), .pDescriptorRanges = ranges.data()}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, .Constants = {.ShaderRegister = 0, .Num32BitValues = 4}},
    }};
    const D3D12_ROOT_SIGNATURE_DESC root_desc = {.NumParameters = static_cast<UINT>(parameters.size()), .pParameters = parameters.data()};
    const D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                                  .NumDescriptors = static_cast<UINT>(moments_.outputs.size() * (MOMENT_PLANES + 1)),
                                                  .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    ComPtr<ID3DBlob> serialized;
    if (FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, serialized.GetAddressOf(), nullptr))
        || FAILED(device_->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(moments_.root.GetAddressOf())))
        || FAILED(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(moments_.heap.GetAddressOf())))) {
      *why = "the moments' root signature or descriptors failed";
      moments_ = {};
      return false;
    }
    const D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline = {.pRootSignature = moments_.root.Get(),
                                                        .CS = {.pShaderBytecode = RT_MOMENTS, .BytecodeLength = sizeof(RT_MOMENTS)}};
    const UINT step = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE handle = moments_.heap->GetCPUDescriptorHandleForHeapStart();
    bool made = SUCCEEDED(device_->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(moments_.pipeline.GetAddressOf())));
    for (MomentOutput& output : moments_.outputs) {
      const D3D12_SHADER_RESOURCE_VIEW_DESC view = {.Format = output.format, .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                                                    .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING, .Texture2D = {.MipLevels = 1}};
      device_->CreateShaderResourceView(output.source, &view, handle);
      handle.ptr += step;
      const uint32_t texels = output.width * output.height;
      for (ComPtr<ID3D12Resource>& plane : output.planes) {
        plane = Buffer(uint64_t{texels} * 16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        made = made && plane;
        const D3D12_UNORDERED_ACCESS_VIEW_DESC planes = {.Format = DXGI_FORMAT_UNKNOWN, .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
                                                         .Buffer = {.NumElements = texels, .StructureByteStride = 16}};
        if (plane) device_->CreateUnorderedAccessView(plane.Get(), nullptr, &planes, handle);
        handle.ptr += step;
      }
    }
    if (!made) {
      *why = "no memory or pipeline for the moments";
      moments_ = {};
      return false;
    }
    return true;
  }
  // This frame's outputs (the request's GPU work done) into the moments, the k-th frame since StartMoments.
  bool AddMoments(std::string* why) {
    ID3D12GraphicsCommandList4* list = RecorderList();
    if (list == nullptr || moments_.outputs.empty()) {
      *why = "the moments have no list or were not started";
      return false;
    }
    ++moments_.frames;
    ID3D12DescriptorHeap* heaps[] = {moments_.heap.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(moments_.root.Get());
    list->SetPipelineState(moments_.pipeline.Get());
    const UINT step = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_GPU_DESCRIPTOR_HANDLE table = moments_.heap->GetGPUDescriptorHandleForHeapStart();
    const D3D12_RESOURCE_BARRIER uav_barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV};
    for (const MomentOutput& output : moments_.outputs) {
      const auto to_read = Transition(output.source, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      list->ResourceBarrier(1, &to_read);
      list->SetComputeRootDescriptorTable(0, table);
      const std::array<uint32_t, 4> constants = {moments_.frames, output.width, output.height, 0};
      list->SetComputeRoot32BitConstants(1, static_cast<UINT>(constants.size()), constants.data(), 0);
      list->Dispatch((output.width + 7) / 8, (output.height + 7) / 8, 1);
      const auto read = Transition(output.source, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
      list->ResourceBarrier(1, &read);
      list->ResourceBarrier(1, &uav_barrier);
      table.ptr += uint64_t{step} * (MOMENT_PLANES + 1);
    }
    if (!WaitFor(SubmitRecorderList(list))) {
      *why = "the moments' pass failed";
      return false;
    }
    return true;
  }
  // The moments read back and let go: each output's file (`file` makes it from the output, the frames added and its planes'
  // float4s, rows from the top) into `folder`/moments_<output>.bin. False: `why`.
  using MomentPlanes = std::array<std::vector<uint8_t>, MOMENT_PLANES>;
  bool WriteMoments(const std::filesystem::path& folder, const std::function<std::vector<uint8_t>(const MomentOutput&, uint32_t, const MomentPlanes&)>& file_of,
                    std::string* why) {
    ID3D12GraphicsCommandList4* list = RecorderList();
    std::vector<Readback> readbacks;
    for (const MomentOutput& output : moments_.outputs) {
      for (const ComPtr<ID3D12Resource>& plane : output.planes) {
        if (list != nullptr) CopyToReadback(list, output.name.c_str(), plane.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &readbacks);
      }
    }
    if (list == nullptr || !WaitFor(SubmitRecorderList(list)) || readbacks.size() != moments_.outputs.size() * MOMENT_PLANES) {
      *why = "the moments could not be read back";
      moments_ = {};
      return false;
    }
    for (size_t index = 0; index < moments_.outputs.size(); ++index) {
      const std::string name = std::format("moments_{}.bin", moments_.outputs[index].name);
      MomentPlanes planes;
      for (uint32_t plane = 0; plane < MOMENT_PLANES; ++plane) {
        if (!ReadbackPayload(&readbacks[(index * MOMENT_PLANES) + plane], &planes[plane])) {
          *why = std::format("{} could not be read", name);
          moments_ = {};
          return false;
        }
        planes[plane].erase(planes[plane].begin(), planes[plane].begin() + sizeof(recording::BufferHeader));
      }
      const std::vector<uint8_t> file = file_of(moments_.outputs[index], moments_.frames, planes);
      if (!(std::ofstream(folder / name, std::ios::binary | std::ios::trunc).write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size())))) {
        *why = std::format("{} could not be written", name);
        moments_ = {};
        return false;
      }
    }
    moments_ = {};
    return true;
  }
#endif

  // One request: the frame's captures into BLASes, the TLAS of what it drew, the trace; the queue waits for the addon's
  // fence value and signals the next (also when nothing ran). `cluster_data`: its new cluster sets' data (v16, the
  // second mapping's start). False: nothing traced.
  bool Trace(const dlaa_bridge::Shared& shared, uint32_t version, std::span<const uint8_t> cluster_data) {
    if (!visibility_) return false;
    // The GPU harness's RENODX_TW_TEST_REMOVAL: the device removed at the process's third trace, as a GPU hang removes it
    // (main.cpp starts a new one).
    static uint32_t test_traces = 0;
    if (++test_traces == 3 && GetEnvironmentVariableW(L"RENODX_TW_TEST_REMOVAL", nullptr, 0) != 0) {
      if (ComPtr<ID3D12Device5> device5; SUCCEEDED(device_.As(&device5))) device5->RemoveDevice();
    }
    // The addon's profile (RT_PROFILE): its file made again at a recording's first trace.
    const bool profiling = (shared.rt_flags & dlaa_bridge::RT_PROFILE) != 0;
    if (profiling && !profiling_) {
      profile_ = std::ofstream(folder_ / L"tw_rt_profile_helper.csv", std::ios::trunc);
      profile_ << "request,frame,kind,cpu_wait_ms,cpu_record_ms,cpu_submit_ms,dynamic_mtriangles,terrain_mtriangles,grass_mtriangles,"
                  "clusters,templated,compact_draws,sets,new_sets,template_builds,meshes,new_blases,micromaps_built,micromaps_waiting,"
                  "instances,placed,cluster_room,static_blas_mb,gpu_scene_ms,gpu_clusters_ms,gpu_tlas_ms,gpu_trace_ms,gpu_paths_ms,"
                  "gpu_a_ms,gpu_b_ms,gpu_c_ms,gpu_d_ms,gpu_bakes,gpu_bakes_ms,cpu_record_parts\n";
    }
    profiling_ = profiling;
    details_ = profiling || (shared.rt_flags & dlaa_bridge::RT_DETAILS) != 0;
    const size_t slot_index = next_slot_;
    Slot& slot = slots_[slot_index];
    next_slot_ = (next_slot_ + 1) % SLOTS;
    LARGE_INTEGER cpu_start;
    QueryPerformanceCounter(&cpu_start);
    WaitFor(slot.value);
    // The last trace done first: recorded over its run the GPU reset at its sway or micromap passes (ATN's debug36). Three
    // requests a frame bring that about (the same slot); with fewer the traces change slots (Light Bounce off).
    WaitFor(trace_value_);
#if defined(RENODX_RECORDER)
    TakeJournalReads(&slot);
    recorded_vertex_end_ = 0;
    recorded_scene_ = {};
#endif
    LARGE_INTEGER cpu_waited;
    QueryPerformanceCounter(&cpu_waited);
    cpu_marks_.clear();
#if defined(RENODX_NRD)
    // The sun's direction this slot's last trace copied (done: SIGMA's light direction a few traces late, the sun moves slowly;
    // after a timed-out wait the GPU may still write it).
    if (slot.sun_copied && !timed_out_) {
      slot.sun_copied = false;
      if (slot.sun_data[3] != 0.f) {
        sun_toward_ = {slot.sun_data[0], slot.sun_data[1], slot.sun_data[2]};
      }
    }
#endif
    // The last trace's placed instances (it is done: its slot's readback).
    if (placed_slot_ != SLOTS) {
      placed_written_ = slots_[placed_slot_].left_out_data[LEFT_OUT_BYTES / 4];
      placed_slot_ = SLOTS;
    }
    // Log Details: the placed instances 60 traces after One Model per Place changed, beside those before (one line a change).
    if (const bool one_at_a_place = (shared.rt_flags & dlaa_bridge::RT_ONE_AT_A_PLACE) != 0; one_at_a_place != placed_rule_) {
      placed_rule_ = one_at_a_place;
      placed_before_rule_ = placed_written_;
      placed_rule_traces_ = 60;
    } else if (placed_rule_traces_ != 0 && --placed_rule_traces_ == 0 && details_) {
      slow_report_ += std::format("{}[TW ray tracing] One Model per Place {}: {} placed instances with those kept, {} before.",
                                  (slow_report_.empty() ? "" : "\n"), (placed_rule_ ? "on" : "off"), placed_written_, placed_before_rule_);
    }
    if (!slot.capture_keys.empty()) {
      const uint32_t* found = slot.left_out_data;
      left_out_.far_off += found[0];
      left_out_.parts += found[1];
      left_out_.others += found[2];
      if (found[3] < slot.capture_keys.size()) {
        left_out_.key = slot.capture_keys[found[3]];
        if (const uint64_t now = GetTickCount64(); now >= garbage_example_time_ + 10000) {
          const auto* example = reinterpret_cast<const float*>(found + 4);
          garbage_example_ = std::format("[TW ray tracing] A garbage triangle (capture {:016X}, flags {}): ({:.6g}, {:.6g}, {:.6g}) ({:.6g}, "
                                         "{:.6g}, {:.6g}) ({:.6g}, {:.6g}, {:.6g}); so far {} far off, {} stretched parts, {} others.",
                                         left_out_.key, slot.capture_flags[found[3]], example[0], example[1], example[2], example[4],
                                         example[5], example[6], example[8], example[9], example[10], left_out_.far_off,
                                         left_out_.parts, left_out_.others);
          garbage_example_time_ = now;
        }
      }
      slot.capture_keys.clear();
    }
    ReadTimes(slot_index);
    slot_traced_[slot_index] = true;
    slot_clustered_[slot_index] = false;
    ExpireHistory(shared.rt_frame);
#if defined(RENODX_RECORDER)
    // The recorder's lock step (a recording's requests, a play-back): every earlier request finished, so what the GPU's
    // progress gives back, reads and bakes below goes by the requests alone.
    if (Lockstep()) WaitFor(own_value_);
#endif
    const uint64_t completed = own_fence_->GetCompletedValue();
    std::erase_if(releases_, [&](const Release& release) {
      if (release.value > completed) return false;
      if (release.units != 0) {
        // A chunk left empty (but a pool's first) lets its buffer go: a burst of builds keeps no memory.
        BlasChunk& chunk = blas_pools_[release.pool][release.chunk];
        chunk.free.Give(release.offset, release.units);
        if (release.chunk != 0 && chunk.free.free.size() == 1 && chunk.free.free.begin()->second == BLAS_CHUNK_UNITS) chunk.buffer.Reset();
      }
      return true;
    });
    std::erase_if(pending_frees_, [&](const PendingFree& pending) {
      if (pending.value > completed) return false;
      free_records_.push_back(pending.record);
      free_triangles_.Give(pending.normal_offset, pending.triangles);
      if (pending.uv_offset != UINT32_MAX) free_uvs_.Give(pending.uv_offset, pending.triangles * 3);
      if (pending.instances != 0) free_instances_.Give(pending.first_instance, pending.instances);
      return true;
    });
    std::erase_if(pending_set_frees_, [&](const PendingSetFree& pending) {
      if (pending.value > completed) return false;
      if (pending.units != 0) free_set_units_.Give(pending.offset, pending.units);
      if (pending.template_first != UINT32_MAX) free_templates_.Give(pending.template_first, pending.templates);
      return true;
    });
    AdvanceMicromaps(completed);
    gpu_timings_.clear();
    cpu_timings_.clear();
    // A removed device's slots keep their last request and passes (LostReport: the lists the GPU stopped in; debug30's
    // named the request after the loss, whose recording failed).
    const bool reset = !Lost() && SUCCEEDED(slot.allocator->Reset()) && SUCCEEDED(slot.list->Reset(slot.allocator.Get(), nullptr));
    if (reset) {
      slot.marks.clear();
      slot.micromap_builds.clear();
      if (details_) {
        slot.request = std::format("trace {}: flags 0x{:X}, sway {} m", own_value_ + 1, shared.rt_flags, (version >= 18) ? shared.rt_sway_range : 0.f);
      }
    }
    // The GPU harness's RENODX_TW_TEST_BAD_LIST: an invalid call in the third trace's list.
    if (reset && test_traces == 3 && GetEnvironmentVariableW(L"RENODX_TW_TEST_BAD_LIST", nullptr, 0) != 0) {
      slot.list->CopyBufferRegion(slot.readback.Get(), 0, slot.sizes.Get(), 0, slot.sizes->GetDesc().Width + sizeof(uint64_t));
    }
    const bool recorded = reset && Record(shared, version, cluster_data, slot_index, &slot);
    if (!recorded) slot.capture_keys.clear();
    if (info_queue_) {
      for (UINT64 index = 0; index < std::min<UINT64>(info_queue_->GetNumStoredMessages(), 20); ++index) {
        SIZE_T bytes = 0;
        info_queue_->GetMessage(index, nullptr, &bytes);
        std::vector<uint8_t> message(bytes);
        if (SUCCEEDED(info_queue_->GetMessage(index, reinterpret_cast<D3D12_MESSAGE*>(message.data()), &bytes))) {
          const auto* found = reinterpret_cast<const D3D12_MESSAGE*>(message.data());
          slow_report_ += std::format("{}[TW ray tracing] D3D12: {}", slow_report_.empty() ? "" : "\n",
                                      std::string_view(found->pDescription, found->DescriptionByteLength));
        }
      }
      info_queue_->ClearStoredMessages();
    }
    LARGE_INTEGER cpu_recorded;
    QueryPerformanceCounter(&cpu_recorded);
    if (details_) {
      cpu_timings_ += std::format(" | CPU ms: wait {:.2f}, record {:.2f}", CpuMs(cpu_start, cpu_waited), CpuMs(cpu_waited, cpu_recorded));
    }
    traced_ = traced_ || recorded;
    if (recorded && (shared.rt_flags & dlaa_bridge::RT_SUN_SHADOWS) != 0) sun_traced_frame_ = shared.rt_frame;
    LARGE_INTEGER submit_start;
    QueryPerformanceCounter(&submit_start);
    if (fence_) {
      queue_->Wait(fence_.Get(), shared.value);
    }
    if (recorded) {
      ID3D12CommandList* lists[] = {slot.list.Get()};
      queue_->ExecuteCommandLists(1, lists);
    }
#if defined(RENODX_RECORDER)
    RecordRequest(shared, version, cluster_data, 1);
#endif
    if (fence_) {
      queue_->Signal(fence_.Get(), shared.value + 1);
    }
    queue_->Signal(own_fence_.Get(), ++own_value_);
    // Replayable (no shared fence, Open): main.cpp signals the addon's next value once this returns.
    if (!fence_) {
      WaitFor(own_value_);
    }
    slot.value = own_value_;
    trace_value_ = own_value_;
    LARGE_INTEGER submit_end;
    QueryPerformanceCounter(&submit_end);
    if (details_) {
      cpu_timings_ += std::format(", submit {:.2f}", CpuMs(submit_start, submit_end));
    }
    // The record's CPU time by pass (each from its mark to the next; "start": before the first).
    slot.profile_parts.clear();
    if (profiling_) {
      LARGE_INTEGER part_start = cpu_waited;
      std::string part = "start";
      cpu_marks_.emplace_back(L"", cpu_recorded);
      for (const auto& [pass, marked] : cpu_marks_) {
        slot.profile_parts += std::format("{}{} {:.3f}", (slot.profile_parts.empty() ? "" : ";"), part, CpuMs(part_start, marked));
        part.assign(pass, pass + std::wcslen(pass));
        part_start = marked;
      }
    }
    slot.profile = profiling_ ? std::format("{},{},trace,{:.3f},{:.3f},{:.3f}{}", own_value_, shared.rt_frame, CpuMs(cpu_start, cpu_waited),
                                            CpuMs(cpu_waited, cpu_recorded), CpuMs(submit_start, submit_end),
                                            recorded ? profile_counts_ : std::string(PROFILE_COUNTS, ','))
                              : std::string();
    return recorded;
  }

  // A request after this frame's trace, against its TLAS (the same queue, so after it): the ray-traced Light Bounce
  // (bridge v7, request 2: rt_bounce.cs_6_5.hlsl; v8 with RT_PATH_TRACING path traced, rt_path.hlsli) or the reflections
  // and specular occlusion (v8, request 3, `reflections`). The radiance cache is filled and resolved at the frame's first
  // such request; last frame's positions are kept for the history. The queue waits for the addon's fence value and
  // signals the next (also when nothing ran). False: not traced.
  bool Paths(const dlaa_bridge::Shared& shared, uint32_t version, bool reflections) {
    if (!visibility_) return false;
    const size_t slot_index = next_slot_;
    Slot& slot = slots_[slot_index];
    next_slot_ = (next_slot_ + 1) % SLOTS;
    LARGE_INTEGER cpu_start;
    QueryPerformanceCounter(&cpu_start);
    WaitFor(slot.value);
    LARGE_INTEGER cpu_waited;
    QueryPerformanceCounter(&cpu_waited);
    ReadTimes(slot_index);
    slot.profile_parts.clear();
    slot_traced_[slot_index] = false;
    slot_reflected_[slot_index] = reflections;
    const uint32_t frame = shared.rt_frame;
    const uint32_t requested = (version >= 8) ? shared.rt_path_flags : 0u;
    const bool path_traced = !reflections && (requested & dlaa_bridge::RT_PATH_TRACING) != 0;
    const bool wanted = reflections ? (reflection_ && (requested & (dlaa_bridge::RT_REFLECTIONS | dlaa_bridge::RT_SPECULAR_OCCLUSION)) != 0)
                                    : (light_ && bounce_);
    // A Light Bounce without paths that the reflections follow (RT_PATH_CACHE_FILL): the frame's history, radiance cache and
    // learned lighting here, from its prepared light, as path tracing's request does (at the reflections' request the water's
    // positions and guides have replaced the scene's, and last frame's colour is the upscaler's input: ATN's broken water).
    const bool fills_cache = !reflections && !path_traced && version >= 28 && (requested & dlaa_bridge::RT_PATH_CACHE_FILL) != 0;
    const bool uses_paths = path_traced || reflections || fills_cache;
    bool recorded = wanted && traced_ && !Lost()
                    && (!uses_paths
                        || (EnsureCache() && EnsureHistory(HISTORY_PATHS, frame) && (!reflections || EnsureHistory(HISTORY_SPECULAR, frame))))
                    && SUCCEEDED(slot.allocator->Reset()) && SUCCEEDED(slot.list->Reset(slot.allocator.Get(), nullptr));
    if (recorded) {
      // (A removed device's slots keep their last request and passes: LostReport.)
      slot.marks.clear();
      if (details_) {
        slot.request = std::format("{} {}: flags 0x{:X}, paths 0x{:X}", reflections ? "reflections" : "Light Bounce", own_value_ + 1, shared.rt_flags,
                                   requested);
      }
      auto* list = slot.list.Get();
      StampSlot(list, slot_index);
#if defined(RENODX_RECORDER)
      ServeStamp(list, SERVE_START);
#endif
      Stamp(list, slot_index * STAMPS + 4);
      const D3D12_RESOURCE_BARRIER uav_barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV};
      ID3D12DescriptorHeap* heaps[] = {descriptors_.Get()};
      list->SetDescriptorHeaps(1, heaps);
      list->SetComputeRootSignature(root_signature_.Get());
      list->SetComputeRootDescriptorTable(ROOT_TABLE, descriptors_->GetGPUDescriptorHandleForHeapStart());
      list->SetComputeRootShaderResourceView(ROOT_SCENE, (ptlas_ != nullptr ? ptlas_ : tlas_.Get())->GetGPUVirtualAddress());
      // Unused root descriptors still point at a buffer.
      list->SetComputeRootShaderResourceView(ROOT_CAPTURE_TABLE, slot.table->GetGPUVirtualAddress());
      for (const Root root : {ROOT_MICROMAP_DATA, ROOT_MICROMAP_INDICES, ROOT_MICROMAP_DESCS}) {
        list->SetComputeRootUnorderedAccessView(root, tlas_scratch_->GetGPUVirtualAddress());
      }
      const float bounce_range = std::clamp(shared.rt_bounce_range, 0.5f, 200.05565f);
      // The Denoiser setting at NRD: REBLUR on this request's light (the Light Bounce at its texels' size; reflections
      // only with the G-buffer's normals and roughness).
      bool nrd_light = false;
      bool nrd_light_ran = false;
#if defined(RENODX_NRD)
      NrdSignal* nrd_signal = nullptr;
      if ((requested & dlaa_bridge::RT_PATH_NRD) != 0) {
        if (reflections) {
          static constexpr std::array<NrdTexture, 4> TEXTURES = {{
              {nrd::ResourceType::IN_VIEWZ, DXGI_FORMAT_R32_FLOAT, NRD_REFLECT_VIEW_Z},
              {nrd::ResourceType::IN_NORMAL_ROUGHNESS, DXGI_FORMAT_R10G10B10A2_UNORM, NRD_REFLECT_NORMAL_ROUGHNESS},
              {nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST, DXGI_FORMAT_R16G16B16A16_FLOAT, NRD_REFLECT_SIGNAL},
              {nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST, DXGI_FORMAT_R16G16B16A16_FLOAT, 0},
          }};
          if (guide_normals_ && shared.rt_guides != 0 && (requested & dlaa_bridge::RT_REFLECTIONS) != 0
              && EnsureNrd(&nrd_reflections_, nrd::Denoiser::REBLUR_SPECULAR, "NRD REBLUR (reflections)", {RT_NRD_FRONT_REFLECT, sizeof(RT_NRD_FRONT_REFLECT)},
                           {RT_NRD_FINISH_REFLECT, sizeof(RT_NRD_FINISH_REFLECT)}, static_cast<uint32_t>(visibility_->GetDesc().Width), visibility_->GetDesc().Height, TEXTURES, frame)) {
            nrd_signal = &nrd_reflections_;
          }
        } else {
          static constexpr std::array<NrdTexture, 4> TEXTURES = {{
              {nrd::ResourceType::IN_VIEWZ, DXGI_FORMAT_R32_FLOAT, NRD_BOUNCE_VIEW_Z},
              {nrd::ResourceType::IN_NORMAL_ROUGHNESS, DXGI_FORMAT_R10G10B10A2_UNORM, NRD_BOUNCE_NORMAL_ROUGHNESS},
              {nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST, DXGI_FORMAT_R16G16B16A16_FLOAT, NRD_BOUNCE_SIGNAL},
              {nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST, DXGI_FORMAT_R16G16B16A16_FLOAT, 0},
          }};
          if (!nrd_bounce_hit_distance_ && Texture(DXGI_FORMAT_R16_FLOAT, &nrd_bounce_hit_distance_)) {
            Written(nrd_bounce_hit_distance_.Get(), DXGI_FORMAT_R16_FLOAT, NRD_BOUNCE_HIT_DISTANCE);
          }
          if (nrd_bounce_hit_distance_
              && EnsureNrd(&nrd_bounce_, nrd::Denoiser::REBLUR_DIFFUSE, "NRD REBLUR (Light Bounce)", {RT_NRD_FRONT_BOUNCE, sizeof(RT_NRD_FRONT_BOUNCE)},
                           {RT_NRD_FINISH_BOUNCE, sizeof(RT_NRD_FINISH_BOUNCE)}, shared.rt_bounce_width, shared.rt_bounce_height, TEXTURES, frame)) {
            nrd_signal = &nrd_bounce_;
          }
        }
        nrd_light = nrd_signal != nullptr;
      }
#endif
      const std::array<uint32_t, 12> settings = {
          frame,
          (!reflections && nrd_light) ? TRACE_NRD_BOUNCE : 0u,
          std::bit_cast<uint32_t>(bounce_range),
          std::clamp(shared.rt_bounce_rays, 1u, 32u),
          shared.rt_width,
          shared.rt_height,
          0u,
          1u,
      };
      std::array<uint32_t, 20> bounce = {};
      std::memcpy(bounce.data(), shared.rt_view_projection, sizeof(shared.rt_view_projection));
      bounce[16] = shared.rt_bounce_width;
      bounce[17] = shared.rt_bounce_height;
      bounce[18] = std::max(shared.rt_bounce_scale, 1u);
      list->SetComputeRoot32BitConstants(ROOT_SETTINGS, static_cast<UINT>(settings.size()), settings.data(), 0);
      list->SetComputeRoot32BitConstants(ROOT_BOUNCE, static_cast<UINT>(bounce.size()), bounce.data(), 0);
      std::vector<std::pair<ID3D12Resource*, D3D12_RESOURCE_STATES>> textures = {
          {positions_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
          {sun_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
      };
      if (light_) textures.emplace_back(light_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      if (!reflections) textures.emplace_back(bounce_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      if (atlas_) textures.emplace_back(atlas_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      // Paths: last frame's colour (still the DLAA input), the guides, the frame's sun visibility, the reflections'.
      const bool guides = guide_normals_ && version >= 8 && shared.rt_guides != 0;
      if (uses_paths) {
        textures.emplace_back(visibility_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (previous_color_) textures.emplace_back(previous_color_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (guides) {
          for (ID3D12Resource* guide : {guide_normals_.Get(), guide_albedo_.Get(), guide_roughness_.Get()}) {
            textures.emplace_back(guide, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
          }
        }
        if (reflections) {
          textures.emplace_back(reflection_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
          textures.emplace_back(specular_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
          if (hit_distance_) textures.emplace_back(hit_distance_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
      }
      std::vector<D3D12_RESOURCE_BARRIER> barriers;
      for (const auto& [resource, used] : textures) barriers.push_back(Transition(resource, D3D12_RESOURCE_STATE_COMMON, used));
      list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
#if defined(RENODX_NRD)
      if (nrd_signal != nullptr) nrd_signal->denoiser->ToWritten(list);
#endif

      if (!uses_paths) {
        Mark(list, L"bounce");
        list->SetPipelineState(bounce_pipeline_.Get());
        list->Dispatch((shared.rt_bounce_width + 7) / 8, (shared.rt_bounce_height + 7) / 8, 1);
        for (const size_t part : {size_t{0}, size_t{1}, size_t{2}}) ProfileStamp(list, slot_index, part);
      } else {
        // The history at the frame's first request: last frame's positions (copied then) kept, this frame's copied.
        const uint32_t width = std::max(shared.rt_width, 1u);
        const uint32_t height = std::max(shared.rt_height, 1u);
        const bool first_paths_request = frame != paths_frame_;
        if (first_paths_request) {
          history_valid_ = paths_copied_frame_ != UINT32_MAX && paths_copied_frame_ + 1 == frame && previous_color_;
          // This frame's trace copied the positions into copied_slice_ for both histories (and turned the slices): a Light
          // Bounce takes its copy (the positions are still the trace's: the water's replace them at the reflections' pass,
          // after it); a reflections request first (the water's may be in them now) hands the trace's copy to
          // ambient_positions_ and makes its own.
          const bool trace_copied = copied_frame_ == frame && trace_positions_slice_ == copied_slice_;
          const D3D12_BOX region = {.left = 0, .top = 0, .front = 0, .right = width, .bottom = height, .back = 1};
          if (trace_copied && reflections) {
            std::array<D3D12_RESOURCE_BARRIER, 2> to_trace = {
                Transition(kept_positions_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE),
                Transition(ambient_positions_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST),
            };
            to_trace[0].Transition.Subresource = copied_slice_;
            list->ResourceBarrier(static_cast<UINT>(to_trace.size()), to_trace.data());
            const D3D12_TEXTURE_COPY_LOCATION to = {.pResource = ambient_positions_.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
            const D3D12_TEXTURE_COPY_LOCATION from = {.pResource = kept_positions_.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
                                                      .SubresourceIndex = copied_slice_};
            list->CopyTextureRegion(&to, 0, 0, 0, &from, &region);
            for (auto& barrier : to_trace) std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            list->ResourceBarrier(static_cast<UINT>(to_trace.size()), to_trace.data());
            trace_positions_slice_ = NO_SLICE;
          }
          if (!trace_copied) {
            // The slice that took the last copy is last frame's now, the other takes this frame's: one copy a frame (the two
            // textures before took two, 0.15 ms of a 4K frame each).
            previous_positions_slice_ = copied_slice_;
            copied_slice_ = 1u - copied_slice_;
            if (trace_positions_slice_ == copied_slice_) trace_positions_slice_ = NO_SLICE;
          }
          if (!trace_copied || reflections) {
            std::array<D3D12_RESOURCE_BARRIER, 2> to_copy = {
                Transition(kept_positions_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST),
                Transition(positions_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
            };
            to_copy[0].Transition.Subresource = copied_slice_;
            list->ResourceBarrier(static_cast<UINT>(to_copy.size()), to_copy.data());
            const D3D12_TEXTURE_COPY_LOCATION to = {.pResource = kept_positions_.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
                                                    .SubresourceIndex = copied_slice_};
            const D3D12_TEXTURE_COPY_LOCATION from = {.pResource = positions_.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
            list->CopyTextureRegion(&to, 0, 0, 0, &from, &region);
            for (auto& barrier : to_copy) std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            list->ResourceBarrier(static_cast<UINT>(to_copy.size()), to_copy.data());
            copied_frame_ = frame;
          }
          std::copy_n(current_view_projection_.begin(), 16, previous_view_projection_.begin());
          std::copy_n(shared.rt_view_projection, 16, current_view_projection_.begin());
          paths_copied_frame_ = frame;
          paths_frame_ = frame;
        }
        // Softer Water Reflections (RT_REFLECTIONS_WATER_POSITIONS): the addon wrote the water's surface over the positions
        // after the frame's first paths request took them (the riverbed's: deep water failed every reflection history test).
        // Last frame's at this request go over the slice the earlier requests read (the next frame's copy takes that slice),
        // and this frame's are kept for the next: two copies at the render size (about 0.15 ms each at 4K).
        if (reflections && !first_paths_request && (requested & dlaa_bridge::RT_REFLECTIONS_WATER_POSITIONS) != 0 && water_frame_ != frame
            && EnsureHistory(HISTORY_WATER, frame)) {
          const D3D12_BOX region = {.left = 0, .top = 0, .front = 0, .right = width, .bottom = height, .back = 1};
          const D3D12_TEXTURE_COPY_LOCATION water = {.pResource = water_positions_.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
          if (water_frame_ != UINT32_MAX && water_frame_ + 1 == frame) {
            std::array<D3D12_RESOURCE_BARRIER, 2> to_previous_copy = {
                Transition(kept_positions_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST),
                Transition(water_positions_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE),
            };
            to_previous_copy[0].Transition.Subresource = previous_positions_slice_;
            if (trace_positions_slice_ == previous_positions_slice_) trace_positions_slice_ = NO_SLICE;
            list->ResourceBarrier(static_cast<UINT>(to_previous_copy.size()), to_previous_copy.data());
            const D3D12_TEXTURE_COPY_LOCATION previous = {.pResource = kept_positions_.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
                                                          .SubresourceIndex = previous_positions_slice_};
            list->CopyTextureRegion(&previous, 0, 0, 0, &water, &region);
            for (auto& barrier : to_previous_copy) std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            list->ResourceBarrier(static_cast<UINT>(to_previous_copy.size()), to_previous_copy.data());
          }
          std::array<D3D12_RESOURCE_BARRIER, 2> to_water_copy = {
              Transition(water_positions_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST),
              Transition(positions_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
          };
          list->ResourceBarrier(static_cast<UINT>(to_water_copy.size()), to_water_copy.data());
          const D3D12_TEXTURE_COPY_LOCATION from = {.pResource = positions_.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
          list->CopyTextureRegion(&water, 0, 0, 0, &from, &region);
          for (auto& barrier : to_water_copy) std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
          list->ResourceBarrier(static_cast<UINT>(to_water_copy.size()), to_water_copy.data());
          water_frame_ = frame;
        }
        auto to_previous = Transition(kept_positions_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        to_previous.Transition.Subresource = previous_positions_slice_;
        list->ResourceBarrier(1, &to_previous);
        if (!reflections) light_frame_ = frame;
        uint32_t flags = 0;
        if (path_traced) flags |= PATH_TRACING;
        if (reflections && (requested & dlaa_bridge::RT_REFLECTIONS) != 0) flags |= PATH_REFLECTIONS;
        if (reflections && (requested & dlaa_bridge::RT_SPECULAR_OCCLUSION) != 0) flags |= PATH_SPECULAR_OCCLUSION;
        if (history_valid_) flags |= PATH_HISTORY_VALID;
        if (cache_filled_) flags |= PATH_CACHE_VALID;
        // Ray Reconstruction denoises the frame: the reflections' hit distance for it (their history and filter stay: RR
        // alone left them noisy, ATN's debug33).
        const bool raw = reflections && (requested & dlaa_bridge::RT_REFLECTIONS_RAW) != 0;
        if (raw) flags |= PATH_RAW;
        if (reflections && (requested & dlaa_bridge::RT_REFLECTIONS_SQUARED_LOBE) != 0) flags |= PATH_SQUARED_LOBE;
        if (reflections && (requested & dlaa_bridge::RT_REFLECTIONS_FOG) != 0) flags |= PATH_REFLECT_FOG;
        if (reflections && (requested & dlaa_bridge::RT_REFLECTIONS_STEADY) != 0) flags |= PATH_REFLECT_STEADY;
        if (nrd_light) flags |= PATH_NRD;
        if (reflections && specular_history_valid_ && specular_frame_ + 1 == frame) flags |= PATH_SPECULAR_HISTORY;
        // Reflection Reuse from ReSTIR GI (v19): this frame's reservoirs (the Light Bounce's request before this one used set
        // reuse_parity_ then and turned it).
        if (reflections && version >= 19 && (requested & dlaa_bridge::RT_REFLECTIONS_REUSE) != 0 && reuse_frame_ == frame) {
          flags |= PATH_REFLECT_REUSE | (reuse_parity_ == 0 ? PATH_REUSE_PARITY : 0u);
        }
        // Own ReSTIR (v19, helpers 55 on): the reflections' own reservoirs, made once a frame (set reflect_parity_, then
        // turned; a second request that frame takes them as made); last frame's when made at this size.
        const bool reflect_own = reflections && guides && (flags & PATH_REFLECTIONS) != 0 && version >= 19
                                 && (requested & dlaa_bridge::RT_REFLECTIONS_OWN) != 0 && EnsureHistory(HISTORY_REFLECT, frame);
        const bool reflect_made = reflect_own && reflect_frame_ == frame;
        if (reflect_own) {
          const uint32_t reflect_set = (reflect_made ? (reflect_parity_ ^ 1u) : reflect_parity_);
          flags |= PATH_REFLECT_OWN | (reflect_set != 0 ? PATH_REFLECT_PARITY : 0u);
          if (!reflect_made && history_valid_ && reflect_frame_ != UINT32_MAX && reflect_frame_ + 1 == frame
              && reflect_size_ == std::array<uint32_t, 2>{width, height}) {
            flags |= PATH_REFLECT_OWN_HISTORY;
          }
        }
        if (guides) flags |= PATH_GUIDES;
        if (sun_traced_frame_ == frame) flags |= PATH_SUN_TRACED;
        if ((shared.rt_flags & dlaa_bridge::RT_START_PAST) != 0) flags |= PATH_START_PAST;
        // A reflections request's light buffer (RT_REFLECTIONS_LIGHT): what their hits read; without the paths' prepared
        // light this frame the radiance cache's too, at the render pixels.
        const bool final_light = reflections && light_ && (requested & dlaa_bridge::RT_REFLECTIONS_LIGHT) != 0;
        if (light_frame_ == frame || final_light) flags |= PATH_LIGHT;
        if (final_light) flags |= PATH_LIGHT_FINAL;
        const bool render_pixels = final_light && light_frame_ != frame;
        // ReSTIR GI (RENODX_TW_NO_REUSE: off); last frame's reservoirs when it ran with these texels.
        const std::array<uint32_t, 3> reuse_size = {shared.rt_bounce_width, shared.rt_bounce_height, std::max(shared.rt_bounce_scale, 1u)};
        const bool reuse = path_traced && (requested & dlaa_bridge::RT_PATH_REUSE) != 0
                           && GetEnvironmentVariableW(L"RENODX_TW_NO_REUSE", nullptr, 0) == 0 && EnsureHistory(HISTORY_REUSE, frame);
        const bool light_was_kept = path_ambient_frame_ != UINT32_MAX && path_ambient_frame_ + 2 >= frame;
        if (path_traced && (requested & dlaa_bridge::RT_PATH_AMBIENT) != 0) {
          flags |= PATH_AMBIENT;
          if ((requested & dlaa_bridge::RT_PATH_SKY_APART) != 0) flags |= PATH_SKY_APART;
          path_ambient_frame_ = frame;
          path_kept_cube_ = (requested & dlaa_bridge::RT_PATH_KEPT_CUBE) != 0;
          path_sun_light_ = (requested & dlaa_bridge::RT_PATH_SUN_LIGHT) != 0;
          if (!light_was_kept) kept_copy_wanted_ = true;
        }
        // The learned lighting stays while the game's ambient is away (also through the reflections' requests: the radiance
        // cache's resolve, once a frame, can be theirs).
        if (path_ambient_frame_ != UINT32_MAX && path_ambient_frame_ + 2 >= frame) {
          flags |= PATH_KEEP_LIGHT | (path_kept_cube_ ? PATH_KEEP_CUBE : 0u) | (path_sun_light_ ? PATH_SUN_LIGHT : 0u);
        }
        // What the light kept at a freeze was (Path-Traced AO's frames after it read this): its copy done once a slot's list has come round.
        if (kept_copy_frame_ != UINT32_MAX && frame > kept_copy_frame_ + SLOTS) {
          const D3D12_RANGE read = {.Begin = 0, .End = sizeof(std::array<float, 4>) * LIGHT_KEPT_TEXELS};
          void* mapped = nullptr;
          if (SUCCEEDED(kept_readback_->Map(0, &read, &mapped))) {
            std::array<std::array<float, 4>, LIGHT_KEPT_TEXELS> kept = {};
            std::memcpy(kept.data(), mapped, sizeof(kept));
            const D3D12_RANGE written = {};
            kept_readback_->Unmap(0, &written);
            const auto& [shade, sunlit, scale, base, ambient] = kept;
            slow_report_ += std::format("{}[TW ray tracing] Path-Traced AO keeps its learned light: shade {:.4f} {:.4f} {:.4f}, the game's ambient {:.4f} {:.4f} {:.4f}, share {:.3f} {:.3f} {:.3f}, sun {:.3f} {:.3f} {:.3f}, base colour {:.3f}.",
                                        slow_report_.empty() ? "" : "\n", shade[0], shade[1], shade[2], ambient[0], ambient[1], ambient[2],
                                        shade[0] / std::max(ambient[0], 1e-4f), shade[1] / std::max(ambient[1], 1e-4f), shade[2] / std::max(ambient[2], 1e-4f),
                                        sunlit[0], sunlit[1], sunlit[2], (base[0] + base[1] + base[2]) / 3.f);
          }
          kept_copy_frame_ = UINT32_MAX;
        }
        if (reuse) {
          flags |= PATH_REUSE | (reuse_parity_ != 0 ? PATH_REUSE_PARITY : 0u);
          if ((requested & dlaa_bridge::RT_PATH_RECONSTRUCTED) != 0) flags |= PATH_PERMUTE;
          // RENODX_TW_REUSE_NO_TEMPORAL (tests): no history merged.
          static const bool no_temporal = GetEnvironmentVariableW(L"RENODX_TW_REUSE_NO_TEMPORAL", nullptr, 0) != 0;
          if (history_valid_ && reuse_frame_ != UINT32_MAX && reuse_frame_ + 1 == frame && reuse_size_ == reuse_size && !no_temporal) {
            flags |= PATH_REUSE_HISTORY;
          }
        }
        // Path-Traced Ambient's own sky at the bounce texels, with the game's sun light (rt_path.hlsli ScreenSky): this frame's
        // paths write set sky_parity_ (turned after them); the light the screen's surfaces send back reads the other, the
        // last paths', at these texels (a reflections request's at the render pixels: none).
        const std::array<uint32_t, 3> sky_size = {render_pixels ? width : shared.rt_bounce_width, render_pixels ? height : shared.rt_bounce_height,
                                                  render_pixels ? 1u : std::max(shared.rt_bounce_scale, 1u)};
        const bool sky_written = path_traced && (flags & PATH_AMBIENT) != 0 && (flags & PATH_SUN_LIGHT) != 0 && EnsureHistory(HISTORY_SKY, frame);
        if (own_sky_[0] && (flags & PATH_SUN_LIGHT) != 0) {
          if (sky_parity_ != 0) flags |= PATH_SKY_PARITY;
          if (history_valid_ && sky_frame_ != UINT32_MAX && sky_frame_ + 1 >= frame && sky_size_ == sky_size) flags |= PATH_SKY_HISTORY;
        }
        // The local lights with their colours (v11): what the paths meet unseen by the screen lit by them too.
        const uint32_t light_count = ((shared.rt_flags & dlaa_bridge::RT_LOCAL_LIGHTS) != 0) ? SharedLightCount(shared, version) : 0u;
        if (light_count != 0) {
          CopyLights(shared, version, light_count, slot.light_data);
          flags |= PATH_LOCAL_LIGHTS;
        }
        list->SetComputeRootConstantBufferView(ROOT_LIGHTS, slot.lights->GetGPUVirtualAddress());
        const uint32_t lights_test = LightsInBounceTest();
        const uint32_t lights_asked = ((requested & dlaa_bridge::RT_PATH_LIGHTS_IN_BOUNCE) != 0)
                                          ? (((requested & dlaa_bridge::RT_PATH_LIGHT_RATIO) != 0) ? 3u : std::max(lights_test, 1u))
                                          : lights_test;
        const bool lights_possible = reuse_lights_pipeline_ && path_traced && reuse && light_count != 0;
        const bool ratio_written = lights_possible && lights_asked == 3u && EnsureHistory(HISTORY_RATIO, frame);
        const uint32_t lights_mode = (lights_possible && (lights_asked != 3u || ratio_written)) ? lights_asked : 0u;
        // Their soft shadows under Ray Reconstruction: the lights' radius (rt_path.hlsli ReuseTexel's rays to their disk).
        if (lights_mode != 0 && (requested & dlaa_bridge::RT_PATH_RECONSTRUCTED) != 0) {
          list->SetComputeRoot32BitConstant(ROOT_SETTINGS, std::bit_cast<uint32_t>(std::clamp(shared.rt_light_size, 0.f, 2.f)), 8);
        }
        // Their ratio's history shorter while the camera moves (RT_PATH_RATIO_FOLLOW): its registers changed since the last
        // paths' beyond a jitter, which adds a multiple of the w row to the x and y rows (what is left over a row's length:
        // radians turned, metres moved; the w row has none).
        const bool ratio_follows = ratio_written && version >= 31 && (requested & dlaa_bridge::RT_PATH_RATIO_FOLLOW) != 0 && [&] {
          const float* w_row = shared.rt_view_projection + 12;
          const float w_length = (w_row[0] * w_row[0]) + (w_row[1] * w_row[1]) + (w_row[2] * w_row[2]);
          for (const size_t row : {size_t{0}, size_t{1}, size_t{3}}) {
            const float* now = shared.rt_view_projection + (row * 4);
            const float* before = previous_view_projection_.data() + (row * 4);
            std::array<float, 4> change = {now[0] - before[0], now[1] - before[1], now[2] - before[2], now[3] - before[3]};
            const float jitter = (row != 3 && w_length > 0.f) ? ((change[0] * w_row[0]) + (change[1] * w_row[1]) + (change[2] * w_row[2])) / w_length : 0.f;
            for (size_t index = 0; index < change.size(); ++index) change[index] -= jitter * w_row[index];
            const float length = std::sqrt((now[0] * now[0]) + (now[1] * now[1]) + (now[2] * now[2]));
            if (length <= 0.f) continue;
            const float turned = std::sqrt((change[0] * change[0]) + (change[1] * change[1]) + (change[2] * change[2])) / length;
            if (turned > 1e-5f || std::abs(change[3]) / length > 1e-3f) return true;
          }
          return false;
        }();
        // RENODX_TW_REUSE_NO_SPATIAL / _NO_VALIDATE / _NO_SKY / _NO_CONTACT / _SEEN_LIGHTS_FRAME (tests, read once): ReSTIR GI's
        // parts left out (the last two the raw paths' too); _SPATIAL_CLAMP / _SKY_DIRECTION / _NO_JACOBIAN_CUT / _HISTORY_PLAIN /
        // _NEIGHBOUR_VISIBILITY / _JACOBIAN_SATURATE its bias suspects (PT DI step 2's G0); RENODX_TW_REUSE_BIAS=1 / 2 the spatial
        // merge's bias correction, Basic / Raytraced (G1), 3 its pairwise MIS (step 5's G2); _BOIL the boiling filter's pass, _BOIL_PLANT its test texel;
        // _RTXDI (the reference build) RTXDI's spatial resampling in the reuse's place, its bias correction RENODX_TW_REUSE_BIAS's;
        // _VALIDATE_* Validate's parts and _NO_HISTORY_CLAMP the history's ClampWeight (part C's bisection) (rt_path.hlsli).
        // The suspects', G1's and Validate's switches (REUSE_SUSPECT_BITS) run only in the reference build's REUSE_TESTS
        // shaders, which take the place of Near / Lean Reuse; the usual paths read only _VALIDATE_KEEP_LIGHT's (Balanced
        // ReSTIR Light's, with its REUSE_BALANCED reuse builds), so that switch alone changes a player build's paths too.
        // Without a log line: in any other build the usual shaders run;
        // with SHaRC its paths do (the reuse is the tests'), with Torch and Fire Light in the Light Bounce its reuse does
        // (the paths are the tests').
        static const uint32_t reuse_tests = [] {
          uint32_t tests = 0;
          for (const auto& [name, bit] : {std::pair{L"RENODX_TW_REUSE_NO_SPATIAL", REUSE_TEST_NO_SPATIAL}, std::pair{L"RENODX_TW_REUSE_NO_VALIDATE", REUSE_TEST_NO_VALIDATE},
                                          std::pair{L"RENODX_TW_REUSE_NO_SKY", REUSE_TEST_NO_SKY}, std::pair{L"RENODX_TW_REUSE_NO_CONTACT", REUSE_TEST_NO_CONTACT},
                                          std::pair{L"RENODX_TW_REUSE_SEEN_LIGHTS_FRAME", REUSE_TEST_SEEN_LIGHTS_FRAME},
                                          std::pair{L"RENODX_TW_REUSE_SPATIAL_CLAMP", REUSE_TEST_SPATIAL_CLAMP}, std::pair{L"RENODX_TW_REUSE_SKY_DIRECTION", REUSE_TEST_SKY_DIRECTION},
                                          std::pair{L"RENODX_TW_REUSE_NO_JACOBIAN_CUT", REUSE_TEST_NO_JACOBIAN_CUT}, std::pair{L"RENODX_TW_REUSE_HISTORY_PLAIN", REUSE_TEST_HISTORY_PLAIN},
                                          std::pair{L"RENODX_TW_REUSE_NEIGHBOUR_VISIBILITY", REUSE_TEST_NEIGHBOUR_VISIBILITY},
                                          std::pair{L"RENODX_TW_REUSE_JACOBIAN_SATURATE", REUSE_TEST_JACOBIAN_SATURATE},
                                          std::pair{L"RENODX_TW_REUSE_BOIL", REUSE_TEST_BOIL}, std::pair{L"RENODX_TW_REUSE_BOIL_PLANT", REUSE_TEST_BOIL_PLANT},
                                          std::pair{L"RENODX_TW_REUSE_RTXDI", REUSE_TEST_RTXDI},
                                          std::pair{L"RENODX_TW_REUSE_VALIDATE_NO_DROP", REUSE_TEST_VALIDATE_NO_DROP},
                                          std::pair{L"RENODX_TW_REUSE_VALIDATE_KEEP_LIGHT", REUSE_TEST_VALIDATE_KEEP_LIGHT},
                                          std::pair{L"RENODX_TW_REUSE_VALIDATE_NO_CUTS", REUSE_TEST_VALIDATE_NO_CUTS},
                                          std::pair{L"RENODX_TW_REUSE_VALIDATE_RESCALE", REUSE_TEST_VALIDATE_RESCALE},
                                          std::pair{L"RENODX_TW_REUSE_VALIDATE_NO_INVALIDITY", REUSE_TEST_VALIDATE_NO_INVALIDITY},
                                          std::pair{L"RENODX_TW_REUSE_VALIDATE_KEEP_CHROMA", REUSE_TEST_VALIDATE_KEEP_CHROMA},
                                          std::pair{L"RENODX_TW_REUSE_NO_HISTORY_CLAMP", REUSE_TEST_NO_HISTORY_CLAMP},
                                          std::pair{L"RENODX_TW_REUSE_VALIDATE_NO_CACHE", REUSE_TEST_VALIDATE_NO_CACHE}}) {
            if (GetEnvironmentVariableW(name, nullptr, 0) != 0) {
              tests |= bit;
            }
          }
          std::array<wchar_t, 8> bias = {};
          if (GetEnvironmentVariableW(L"RENODX_TW_REUSE_BIAS", bias.data(), static_cast<DWORD>(bias.size())) != 0) {
            switch (std::wcstoul(bias.data(), nullptr, 10)) {
              case 1:
                tests |= REUSE_BIAS_BASIC;
                break;
              case 2:
                tests |= REUSE_BIAS_RAYTRACED;
                break;
              case 3:
                tests |= REUSE_BIAS_PAIRWISE;
                break;
              default:
                break;
            }
          }
          return tests;
        }();
        // Balanced ReSTIR Light (RT_PATH_REUSE_BALANCED, older addons never set it) where its twins were made: Validate's
        // kept light in the paths' constants and the reuse's REUSE_BALANCED twin (G1 Basic), both or neither; with
        // RT_PATH_REUSE_BALANCED_RAYTRACED (older addons never set it) its REUSE_BALANCED_RAYTRACED twin (G1 Raytraced) where
        // those were made, else the Basic one.
        const bool balanced = (requested & dlaa_bridge::RT_PATH_REUSE_BALANCED) != 0 && reuse_balanced_pipelines_[0];
        const bool torch_light = path_traced && light_count != 0 && (requested & dlaa_bridge::RT_PATH_TORCH_LIGHT) != 0;
#if defined(RENODX_REGIR)
        // Torch Light in Path-Traced GI's ReGIR (PT DI step 3, T2): RTXDI's onion of cells around the camera (its defaults,
        // 5 detail and 10 coverage layers, the radii in cells: REGIR_CELL_SIZE), REGIR_LIGHTS_PER_CELL lights a cell picked
        // uniformly from REGIR_BUILD_SAMPLES, no jitter (every cell's lights all that may reach it); uniform outside.
        static const auto [onion_parameters, onion_slots] = [] {
          rtxdi::RISBufferSegmentAllocator cells;
          const rtxdi::ReGIRContext context({.mode = rtxdi::ReGIRMode::Onion, .lightsPerCell = REGIR_LIGHTS_PER_CELL}, cells);
          const rtxdi::ReGIROnionCalculatedParameters calculated = context.GetReGIROnionCalculatedParameters();
          ReGIR_Parameters parameters = {.commonParams = {.localLightSamplingFallbackMode = REGIR_LOCAL_LIGHT_FALLBACK_MODE_UNIFORM,
                                                          .risBufferOffset = context.GetReGIRCellOffset(),
                                                          .lightsPerCell = REGIR_LIGHTS_PER_CELL,
                                                          .cellSize = REGIR_CELL_SIZE,
                                                          .samplingJitter = 0.f,
                                                          .localLightPresamplingMode = REGIR_LOCAL_LIGHT_PRESAMPLING_MODE_UNIFORM,
                                                          .numRegirBuildSamples = REGIR_BUILD_SAMPLES}};
          for (size_t index = 0; index < calculated.regirOnionLayers.size(); ++index) {
            parameters.onionParams.layers[index] = calculated.regirOnionLayers[index];
            parameters.onionParams.layers[index].innerRadius *= REGIR_CELL_SIZE;
            parameters.onionParams.layers[index].outerRadius *= REGIR_CELL_SIZE;
          }
          std::ranges::copy(calculated.regirOnionRings, parameters.onionParams.rings);
          parameters.onionParams.numLayerGroups = static_cast<uint32_t>(calculated.regirOnionLayers.size());
          parameters.onionParams.cubicRootFactor = calculated.regirOnionCubicRootFactor;
          parameters.onionParams.linearFactor = calculated.regirOnionLinearFactor;
          return std::pair{parameters, context.GetReGIRLightSlotCount()};
        }();
        const bool regir = torch_light && regir_pipeline_ && GrowBuffer(std::addressof(regir_cells_), uint64_t{onion_slots} * 8, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        // T1: the trace's torch light at the render pixels this frame (the last paths' row on); only with T2, which covers
        // the same lights (without ReGIR TorchLight is LocalLights: the first 32).
        if (torch_light) {
          torch_light_frame_ = frame;
        }
        const bool torch_traced = regir && torch_irradiance_frame_ == frame && torch_irradiance_;
#else
        constexpr bool regir = false;
        constexpr bool torch_traced = false;
#endif
        PathConstants constants = {
            .bounce_width = render_pixels ? width : shared.rt_bounce_width,
            .bounce_height = render_pixels ? height : shared.rt_bounce_height,
            .bounce_scale = render_pixels ? 1u : std::max(shared.rt_bounce_scale, 1u),
            .flags = flags,
            .bounces = std::clamp(shared.rt_path_bounces, 0u, 8u),
            // ReSTIR GI under Ray Reconstruction (the trace's RT_RECONSTRUCTED: also with REBLUR on the Light Bounce): one path a
            // texel whatever Light Bounce Rays or Match Output asked (its bias rose with the rays: G15's scorer; ATN 11-22).
            .samples = (reuse && (shared.rt_flags & dlaa_bridge::RT_RECONSTRUCTED) != 0) ? 1u : std::clamp(shared.rt_path_samples, 1u, 16u),
            .bounce_range = bounce_range,
            .reflection_range = std::clamp(shared.rt_reflection_range, 0.5f, 200.02019f),
            .sky_range = std::clamp(shared.rt_ambient_range, 0.1f, 100.000854f),
            .previous_square_root = (version >= 28 && (requested & dlaa_bridge::RT_PATH_COLOR_SQUARE_ROOT) != 0) ? 1u : 0u,
            .previous_positions_slice = previous_positions_slice_,
            // Torch and Fire Light in the Light Bounce: ReuseTexel shades the lights' samples (the paths with ReSTIR GI), or
            // keeps their shadow ratio (ratio_written below). Torch Light in Path-Traced GI (RT_PATH_TORCH_LIGHT): the
            // local lights on the screen's surfaces a path meets too (LIGHTS_IN_PATHS).
            .lights_in_bounce = lights_mode | ((ratio_written && ratio_parity_ != 0) ? 16u : 0u)
                                | ((ratio_written && history_valid_ && ratio_frame_ != UINT32_MAX && ratio_frame_ + 1 == frame && ratio_size_ == reuse_size) ? 32u : 0u)
                                | (ratio_follows ? 64u : 0u)
                                | (torch_light ? LIGHTS_IN_PATHS : 0u) | (regir ? LIGHTS_REGIR : 0u) | (torch_traced ? LIGHTS_TORCH_IRRADIANCE : 0u),
            // Balanced ReSTIR Light's kept light: the test bit the player's paths read (rt_path.hlsli REUSE_SUSPECTS; the
            // tests pipelines' choice stays the environment's).
            .reuse_tests = reuse_tests | (balanced ? REUSE_TEST_VALIDATE_KEEP_LIGHT : 0u),
        };
        std::copy_n(shared.rt_view_projection, 16, constants.view_projection.begin());
        std::copy_n(previous_view_projection_.begin(), 16, constants.previous_view_projection.begin());
#if defined(RENODX_PATH_PROBE)
        // RENODX_TW_PATH_PROBE=<mode>[,<rays a texel>] (tests): the paths' probe build at the bounce texels (rt_path.hlsli ProbeTexel).
        static const uint32_t probe_mode = [] {
          std::array<wchar_t, 32> value = {};
          if (GetEnvironmentVariableW(L"RENODX_TW_PATH_PROBE", value.data(), static_cast<DWORD>(value.size())) == 0) return 0u;
          wchar_t* rays = nullptr;
          const auto mode = static_cast<uint32_t>(std::wcstoul(value.data(), &rays, 10));
          return mode | (static_cast<uint32_t>((*rays == L',') ? std::wcstoul(rays + 1, nullptr, 10) : 64u) << 8);
        }();
        constants.probe_mode = path_probe_pipeline_ ? probe_mode : 0u;
#endif
        std::memcpy(slot.paths_data, &constants, sizeof(constants));
        list->SetComputeRootConstantBufferView(ROOT_PATHS, slot.paths->GetGPUVirtualAddress());
#if defined(RENODX_REGIR)
        if (regir) {
          // The onion's centre: the camera, where the clip's x, y and w are 0 (rows 0, 1, 3; Cramer's rule; at the origin
          // for a view without one: the cells then off centre, still unbiased).
          ReGIR_Parameters parameters = onion_parameters;
          const float* vp = shared.rt_view_projection;
          const auto row = [&](size_t index) { return std::array<double, 4>{vp[index * 4], vp[(index * 4) + 1], vp[(index * 4) + 2], vp[(index * 4) + 3]}; };
          const std::array<std::array<double, 4>, 3> rows = {row(0), row(1), row(3)};
          const auto determinant = [&](size_t replaced) {
            const auto at = [&](size_t r, size_t c) { return (c == replaced) ? -rows[r][3] : rows[r][c]; };
            return (at(0, 0) * ((at(1, 1) * at(2, 2)) - (at(1, 2) * at(2, 1)))) - (at(0, 1) * ((at(1, 0) * at(2, 2)) - (at(1, 2) * at(2, 0))))
                   + (at(0, 2) * ((at(1, 0) * at(2, 1)) - (at(1, 1) * at(2, 0))));
          };
          const double whole = determinant(3);
          if (std::abs(whole) > 1e-12) {
            parameters.commonParams.centerX = static_cast<float>(determinant(0) / whole);
            parameters.commonParams.centerY = static_cast<float>(determinant(1) / whole);
            parameters.commonParams.centerZ = static_cast<float>(determinant(2) / whole);
          }
          std::memcpy(slot.light_data + REGIR_OFFSET, &parameters, sizeof(parameters));
          const D3D12_UNORDERED_ACCESS_VIEW_DESC cells_view = {.Format = DXGI_FORMAT_R32G32_UINT, .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
                                                               .Buffer = {.NumElements = onion_slots}};
          device_->CreateUnorderedAccessView(regir_cells_.Get(), nullptr, &cells_view, Descriptor(REGIR_CELLS));
          Mark(list, L"ReGIR cells");
          list->SetPipelineState(regir_pipeline_.Get());
          list->Dispatch((onion_slots + 63) / 64, 1, 1);
          list->ResourceBarrier(1, &uav_barrier);
        }
        if (torch_traced) {
          Written(torch_irradiance_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, TORCH_IRRADIANCE);
        }
#endif
        // The radiance cache once a frame: filled (from the prepared light, else last frame's colour; with the guides each
        // cell's owner), resolved (the owners' base colours).
        if (cache_frame_ != frame) {
          const bool from_light = (flags & PATH_LIGHT) != 0;
          const uint32_t fill_width = (from_light ? constants.bounce_width : width) / 2 + 1;
          const uint32_t fill_height = (from_light ? constants.bounce_height : height) / 2 + 1;
          Mark(list, L"radiance cache");
          list->SetPipelineState(cache_fill_pipeline_.Get());
          list->SetComputeRootUnorderedAccessView(ROOT_SCRATCH, cache_owners_->GetGPUVirtualAddress());
          list->Dispatch((fill_width + 7) / 8, (fill_height + 7) / 8, 1);
          list->ResourceBarrier(1, &uav_barrier);
          list->SetPipelineState(cache_resolve_pipeline_.Get());
          list->Dispatch(CACHE_ENTRIES / 64, 1, 1);
          list->ResourceBarrier(1, &uav_barrier);
          if (kept_copy_wanted_ && kept_readback_) {
            const auto to_copy = Transition(light_kept_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            list->ResourceBarrier(1, &to_copy);
            list->CopyBufferRegion(kept_readback_.Get(), 0, light_kept_.Get(), 0, sizeof(std::array<float, 4>) * LIGHT_KEPT_TEXELS);
            const auto copied = Transition(light_kept_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            list->ResourceBarrier(1, &copied);
            kept_copy_wanted_ = false;
            kept_copy_frame_ = frame;
          }
          cache_frame_ = frame;
          cache_filled_ = true;
        }
        // SHaRC Radiance Cache (bridge v18, with Path Tracing): its buffers at u6-u8, its update (a bounce texel of each
        // SHARC_BLOCK x SHARC_BLOCK) and resolve, then the paths read it (their SHaRC variant). Not the reflections: their
        // SHaRC variant hung the GPU at 4K in the harness (the 5090).
        bool sharc = sharc_path_pipeline_ && path_traced && version >= 18 && (requested & dlaa_bridge::RT_PATH_SHARC) != 0;
        // Its cache is kept from frame to frame (SHaRC's resolve blends and evicts it); paths without it give it back
        // once the GPU is past this request, so the row on again starts from zeroed buffers (SHaRC's reset).
        if (path_traced && !sharc && sharc_buffers_[0]) {
          for (ComPtr<ID3D12Resource>& buffer : sharc_buffers_) {
            if (buffer) releases_.push_back({.value = own_value_ + 1, .resource = std::move(buffer)});
          }
        }
        for (size_t index = 0; index < sharc_buffers_.size() && sharc; ++index) {
          sharc = GrowBuffer(std::addressof(sharc_buffers_[index]), uint64_t{SHARC_ENTRIES} * SHARC_STRIDES[index], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        if (sharc) {
          list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_DATA, sharc_buffers_[0]->GetGPUVirtualAddress());
          list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_INDICES, sharc_buffers_[1]->GetGPUVirtualAddress());
          list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_DESCS, sharc_buffers_[2]->GetGPUVirtualAddress());
          if (sharc_frame_ != frame) {
            const uint32_t blocks_x = (shared.rt_bounce_width + SHARC_BLOCK - 1) / SHARC_BLOCK;
            const uint32_t blocks_y = (shared.rt_bounce_height + SHARC_BLOCK - 1) / SHARC_BLOCK;
            Mark(list, L"SHaRC");
            const ComPtr<ID3D12PipelineState>& update_state = ((regir && sharc_update_regir_pipeline_) ? sharc_update_regir_pipeline_ : sharc_update_pipeline_);
            list->SetPipelineState(update_state.Get());
            list->Dispatch((blocks_x + 7) / 8, (blocks_y + 7) / 8, 1);
            list->ResourceBarrier(1, &uav_barrier);
            list->SetPipelineState(sharc_resolve_pipeline_.Get());
            list->Dispatch(SHARC_ENTRIES / 64, 1, 1);
            list->ResourceBarrier(1, &uav_barrier);
            sharc_frame_ = frame;
          }
        }
        ProfileStamp(list, slot_index, 0);
        Mark(list, L"path tracing");
        // With Path Tracing SER (bridge v18) the paths' ray generation shaders.
        const bool ser = path_ser_ && version >= 18 && (requested & dlaa_bridge::RT_PATH_SER) != 0;
        // ReSTIR GI's suspects or bias correction asked (tests): the compute paths' and the reuse's REUSE_TESTS builds (not
        // with SHaRC; with SER the same picture). Declared before the probe's branch, which takes the chain below as its else.
        const bool reuse_suspects = reuse && path_tests_pipeline_ && (reuse_tests & REUSE_SUSPECT_BITS) != 0;
#if defined(RENODX_PATH_PROBE)
        if (path_traced && constants.probe_mode != 0) {
          list->SetPipelineState(path_probe_pipeline_.Get());
          list->Dispatch((shared.rt_bounce_width + 7) / 8, (shared.rt_bounce_height + 7) / 8, 1);
        } else
#endif
        if (path_traced && reuse_suspects && !sharc) {
          list->SetPipelineState(path_tests_pipeline_.Get());
          list->Dispatch((shared.rt_bounce_width + 7) / 8, (shared.rt_bounce_height + 7) / 8, 1);
        } else if (path_traced && sharc) {
          // One ray a texel: its own build, as without SHaRC.
          list->SetPipelineState((constants.samples == 1) ? sharc_path_one_pipeline_.Get() : sharc_path_pipeline_.Get());
          list->Dispatch((shared.rt_bounce_width + 7) / 8, (shared.rt_bounce_height + 7) / 8, 1);
        } else if (path_traced && ser) {
          // One ray a texel: its own build, as the compute paths'.
          const bool one_sample = constants.samples == 1 && path_ser_one_;
          list->SetPipelineState1(one_sample ? path_ser_one_.Get() : path_ser_.Get());
          const D3D12_DISPATCH_RAYS_DESC rays = {
              .RayGenerationShaderRecord = {.StartAddress = path_ser_table_->GetGPUVirtualAddress() + (one_sample ? PATH_SER_RECORD : 0),
                                            .SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES},
              .Width = shared.rt_bounce_width,
              .Height = shared.rt_bounce_height,
              .Depth = 1,
          };
          list->DispatchRays(&rays);
        } else if (path_traced) {
          // One ray a texel: its own build (the same picture in fewer registers).
          list->SetPipelineState(((constants.samples == 1 && path_one_pipeline_) ? path_one_pipeline_ : path_pipeline_).Get());
          list->Dispatch((shared.rt_bounce_width + 7) / 8, (shared.rt_bounce_height + 7) / 8, 1);
        } else if (fills_cache) {
          Mark(list, L"bounce");
          list->SetPipelineState(bounce_pipeline_.Get());
          list->Dispatch((shared.rt_bounce_width + 7) / 8, (shared.rt_bounce_height + 7) / 8, 1);
        }
        if (sky_written) {
          sky_frame_ = frame;
          sky_parity_ ^= 1u;
          sky_size_ = sky_size;
        }
        ProfileStamp(list, slot_index, 1);
        if (reuse) {
          list->ResourceBarrier(1, &uav_barrier);
          // The boiling filter on this frame's reservoirs before their reuse (rt_path.hlsli BoilTexel; 8 x 8 groups, RTXDI's).
          if (reuse_boil_pipeline_ && (reuse_tests & (REUSE_TEST_BOIL | REUSE_TEST_BOIL_PLANT)) != 0) {
            Mark(list, L"boiling filter");
            list->SetPipelineState(reuse_boil_pipeline_.Get());
            list->Dispatch((shared.rt_bounce_width + 7) / 8, (shared.rt_bounce_height + 7) / 8, 1);
            list->ResourceBarrier(1, &uav_barrier);
          }
          // The RTXDI GI contest (the reference build): this frame's reservoirs in RTXDI's format and its neighbour offsets
          // (u73 / u74, rt_path.hlsli RtxdiPackTexel; made at the shared textures' size when first asked for), then RTXDI's
          // spatial resampling in the reuse's place.
          bool rtxdi = lights_mode == 0 && reuse_rtxdi_pipeline_ && (reuse_tests & REUSE_TEST_RTXDI) != 0;
          if (rtxdi && !rtxdi_reservoirs_) {
            const D3D12_RESOURCE_DESC positions = positions_->GetDesc();
            const uint64_t reservoirs = ((positions.Width + 15) / 16) * ((positions.Height + 15) / 16) * 256;  // RTXDI's 16 x 16 blocks
            rtxdi_reservoirs_ = Buffer(reservoirs * RTXDI_RESERVOIR_BYTES, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            rtxdi_offsets_ = Buffer(uint64_t{RTXDI_OFFSET_COUNT} * 8, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            if (!rtxdi_reservoirs_ || !rtxdi_offsets_) {
              // Said once: the contest stays off (Buffer() reports no failure of its own).
              slow_report_ += std::format("{}[TW ray tracing] RENODX_TW_REUSE_RTXDI: no memory for its {} MB of reservoirs: off",
                                          slow_report_.empty() ? "" : "\n", (reservoirs * RTXDI_RESERVOIR_BYTES) >> 20);
              rtxdi_reservoirs_.Reset();
              rtxdi_offsets_.Reset();
              reuse_rtxdi_pipeline_.Reset();
            }
          }
          rtxdi = rtxdi && rtxdi_reservoirs_ && rtxdi_offsets_;
          if (rtxdi) {
            // Written each such frame (WriteNullPathDescriptors nulls them when the cache's pools fail).
            const D3D12_UNORDERED_ACCESS_VIEW_DESC reservoirs_view = {
                .Format = DXGI_FORMAT_UNKNOWN, .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
                .Buffer = {.NumElements = static_cast<UINT>(rtxdi_reservoirs_->GetDesc().Width / RTXDI_RESERVOIR_BYTES), .StructureByteStride = RTXDI_RESERVOIR_BYTES}};
            const D3D12_UNORDERED_ACCESS_VIEW_DESC offsets_view = {.Format = DXGI_FORMAT_R32G32_FLOAT, .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
                                                                   .Buffer = {.NumElements = RTXDI_OFFSET_COUNT}};
            device_->CreateUnorderedAccessView(rtxdi_reservoirs_.Get(), nullptr, &reservoirs_view, Descriptor(RTXDI_GI_RESERVOIRS));
            device_->CreateUnorderedAccessView(rtxdi_offsets_.Get(), nullptr, &offsets_view, Descriptor(RTXDI_OFFSETS));
            Mark(list, L"RTXDI GI pack");
            list->SetPipelineState(reuse_rtxdi_pack_pipeline_.Get());
            list->Dispatch((shared.rt_bounce_width + 7) / 8, (shared.rt_bounce_height + 7) / 8, 1);
            list->ResourceBarrier(1, &uav_barrier);
          }
          Mark(list, L"reuse");
          // Lean ReSTIR Reuse (RT_PATH_REUSE_LEAN, or RENODX_TW_REUSE_LEAN for the harness): the neighbours with fewer
          // fetches; Near ReSTIR Reuse (RT_PATH_REUSE_NEAR, RENODX_TW_REUSE_NEAR), before it: fewer, nearer ones
          // (rt_path.hlsli ReuseTexel); Matched ReSTIR Neighbours (RT_RESTIR_COMPATIBLE, RENODX_TW_REUSE_COMPATIBLE), before
          // both: three of eight near neighbours picked by how well their surfaces match.
          const bool compatible = reuse_compatible_pipeline_
                                  && ((version >= 32 && (shared.rt_restir_flags & dlaa_bridge::RT_RESTIR_COMPATIBLE) != 0)
                                      || GetEnvironmentVariableW(L"RENODX_TW_REUSE_COMPATIBLE", nullptr, 0) != 0);
          const bool lean = reuse_lean_pipeline_
                            && ((version >= 31 && (requested & dlaa_bridge::RT_PATH_REUSE_LEAN) != 0)
                                || GetEnvironmentVariableW(L"RENODX_TW_REUSE_LEAN", nullptr, 0) != 0);
          const bool nearer = reuse_near_pipeline_
                              && ((version >= 31 && (requested & dlaa_bridge::RT_PATH_REUSE_NEAR) != 0)
                                  || GetEnvironmentVariableW(L"RENODX_TW_REUSE_NEAR", nullptr, 0) != 0);
          ID3D12PipelineState* reuse_state = reuse_pipeline_.Get();
          if (lights_mode != 0) {
            reuse_state = reuse_lights_pipeline_.Get();
          } else if (rtxdi) {
            reuse_state = reuse_rtxdi_pipeline_.Get();
          } else if (reuse_suspects) {
            reuse_state = reuse_tests_pipeline_.Get();
          } else if (compatible) {
            reuse_state = reuse_compatible_pipeline_.Get();
          } else if (nearer) {
            reuse_state = reuse_near_pipeline_.Get();
          } else if (lean) {
            reuse_state = reuse_lean_pipeline_.Get();
          }
          // Balanced ReSTIR Light: the usual reuse build's REUSE_BALANCED or REUSE_BALANCED_RAYTRACED twin (G1 Basic or
          // Raytraced; RTXDI's and the tests' have none). It also takes the place of a SwapStem("rt_reuse") swap: lab A/B jobs
          // that swap rt_reuse keep bit 28 clear.
          uint32_t balanced_mode = 0;  // the one that runs: 0 none, 1 Basic, 2 Raytraced
          if (balanced) {
            const bool balanced_raytraced = (requested & dlaa_bridge::RT_PATH_REUSE_BALANCED_RAYTRACED) != 0 && reuse_balanced_raytraced_pipelines_[0];
            const std::array<BalancedTwin, 4> twins = BalancedTwins();
            const auto twin = std::ranges::find_if(twins, [&](const BalancedTwin& each) { return (this->*each.plain).Get() == reuse_state; });
            if (twin != twins.end()) {
              const std::array<ComPtr<ID3D12PipelineState>, 4>& twin_pipelines = (balanced_raytraced ? reuse_balanced_raytraced_pipelines_ : reuse_balanced_pipelines_);
              reuse_state = twin_pipelines[twin - twins.begin()].Get();
              balanced_mode = balanced_raytraced ? 2u : 1u;
            }
            if (compatible && reuse_state == reuse_compatible_pipeline_.Get()) {
              const bool compatible_raytraced = balanced_raytraced && reuse_compatible_balanced_raytraced_pipeline_;
              const ComPtr<ID3D12PipelineState>& compatible_twin = (compatible_raytraced ? reuse_compatible_balanced_raytraced_pipeline_ : reuse_compatible_balanced_pipeline_);
              reuse_state = compatible_twin.Get();
              balanced_mode = compatible_raytraced ? 2u : 1u;
            }
          }
          if (balanced_mode != balanced_logged_) {
            balanced_logged_ = balanced_mode;
            constexpr std::array<const char*, 3> BALANCED_MODES = {"off", "Basic", "Raytraced"};
            slow_report_ += std::format("{}[TW ray tracing] Balanced ReSTIR Light: {}.", (slow_report_.empty() ? "" : "\n"), BALANCED_MODES[balanced_mode]);
          }
          list->SetPipelineState(reuse_state);
          list->Dispatch((shared.rt_bounce_width + 7) / 8, (shared.rt_bounce_height + 7) / 8, 1);
          reuse_frame_ = frame;
          reuse_lights_mode_ = lights_mode;
          reuse_parity_ ^= 1u;
          reuse_size_ = reuse_size;
          if (ratio_written) {
            ratio_frame_ = frame;
            ratio_parity_ ^= 1u;
            ratio_size_ = reuse_size;
          }
        }
        ProfileStamp(list, slot_index, 2);
        if (reflect_own && !reflect_made) {
          Mark(list, L"reflection reservoirs");
          list->SetPipelineState(reflect_reservoir_pipeline_.Get());
          list->Dispatch(((width + 1) / 2 + 7) / 8, ((height + 1) / 2 + 7) / 8, 1);
          list->ResourceBarrier(1, &uav_barrier);
          reflect_frame_ = frame;
          reflect_parity_ ^= 1u;
          reflect_size_ = {width, height};
        }
        if (reflections) {
          Mark(list, L"reflections");
          // One sample a pixel: its own build (the same picture; the general one spilled registers at 4K). Not reordered
          // under Path Tracing SER (a mirror or lobe ray a pixel: nothing to regroup).
          list->SetPipelineState(((constants.samples == 1 && reflect_one_pipeline_) ? reflect_one_pipeline_ : reflect_pipeline_).Get());
          list->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
          list->ResourceBarrier(1, &uav_barrier);
          list->SetPipelineState(reflect_filter_pipeline_.Get());
          list->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
          specular_frame_ = frame;
          specular_history_valid_ = true;
        }
        // Unused root descriptors still point at a buffer.
        if (sharc) {
          for (const Root root : {ROOT_MICROMAP_DATA, ROOT_MICROMAP_INDICES, ROOT_MICROMAP_DESCS}) {
            list->SetComputeRootUnorderedAccessView(root, tlas_scratch_->GetGPUVirtualAddress());
          }
        }
#if defined(RENODX_REFERENCE)
        ReferenceRequest(list, {.kind = reflections ? ReferenceKind::REFLECTIONS : ReferenceKind::BOUNCE, .frame = frame, .render = {width, height},
                                .bounce = {constants.bounce_width, constants.bounce_height}, .settings = &settings, .bounce_constants = &bounce,
                                .paths = slot.paths->GetGPUVirtualAddress(), .lights = slot.lights->GetGPUVirtualAddress(),
                                .capture_table = slot.table->GetGPUVirtualAddress(), .path_constants = &constants, .light_data = slot.light_data,
                                .guides = guides});
#endif
        std::swap(to_previous.Transition.StateBefore, to_previous.Transition.StateAfter);
        list->ResourceBarrier(1, &to_previous);
      }
#if defined(RENODX_NRD)
      if (nrd_signal != nullptr) {
        // Its inputs (on this list's bindings), then REBLUR (on its own: nothing below needs these).
        const uint32_t nrd_width = reflections ? std::max(shared.rt_width, 1u) : shared.rt_bounce_width;
        const uint32_t nrd_height = reflections ? std::max(shared.rt_height, 1u) : shared.rt_bounce_height;
        list->ResourceBarrier(1, &uav_barrier);
        Mark(list, L"NRD inputs");
        list->SetPipelineState(nrd_signal->front.Get());
        list->Dispatch((nrd_width + 7) / 8, (nrd_height + 7) / 8, 1);
        list->ResourceBarrier(1, &uav_barrier);
        Mark(list, L"NRD REBLUR");
        const nrd::ReblurSettings reblur = {};
        nrd_light_ran = nrd_signal->denoiser->Run(list, static_cast<UINT>(slot_index), shared.rt_view_projection, frame, nrd_width, nrd_height, &reblur);
        if (nrd_light_ran) {
          ID3D12Resource* const output = reflections ? reflection_.Get() : bounce_.Get();
          nrd_signal->denoiser->Finish(list, reflections ? nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST : nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST, positions_.Get(),
                                       POSITIONS_VIEW, output, DXGI_FORMAT_R16G16B16A16_FLOAT);
        }
      }
#endif
      if ((requested & dlaa_bridge::RT_PATH_NRD) != 0) {
        NoteNrd(reflections ? dlaa_bridge::RT_NRD_OFF_REFLECTIONS : dlaa_bridge::RT_NRD_OFF_BOUNCE, nrd_light_ran);
      }
      barriers.clear();
      for (const auto& [resource, used] : textures) barriers.push_back(Transition(resource, used, D3D12_RESOURCE_STATE_COMMON));
      list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
      Stamp(list, slot_index * STAMPS + 5);
      Resolve(list, slot_index);
      if (const HRESULT closed = list->Close(); FAILED(closed)) {
        recorded = false;
        request_failed_ = true;
        status_ = std::format("Ray tracing: a request failed (0x{:08X})", static_cast<uint32_t>(closed));
        slow_report_ += std::format("{}[TW ray tracing] {} {} could not be recorded (0x{:08X}); its passes: {}.", slow_report_.empty() ? "" : "\n",
                                    reflections ? "Reflections" : "Light Bounce", own_value_ + 1, static_cast<uint32_t>(closed), Passes(slot));
      }
    }
    LARGE_INTEGER submit_start;
    QueryPerformanceCounter(&submit_start);
    if (fence_) {
      queue_->Wait(fence_.Get(), shared.value);
    }
    if (recorded) {
      ID3D12CommandList* lists[] = {slot.list.Get()};
      queue_->ExecuteCommandLists(1, lists);
    }
#if defined(RENODX_RECORDER)
    RecordRequest(shared, version, {}, reflections ? 3u : 2u);
#endif
    if (fence_) {
      queue_->Signal(fence_.Get(), shared.value + 1);
    }
    queue_->Signal(own_fence_.Get(), ++own_value_);
    // Replayable (no shared fence, Open): main.cpp signals the addon's next value once this returns.
    if (!fence_) {
      WaitFor(own_value_);
    }
    slot.value = own_value_;
    LARGE_INTEGER submit_end;
    QueryPerformanceCounter(&submit_end);
    slot.profile = profiling_ ? std::format("{},{},{},{:.3f},{:.3f},{:.3f}{}", own_value_, frame, reflections ? "reflections" : "bounce",
                                            CpuMs(cpu_start, cpu_waited), CpuMs(cpu_waited, submit_start), CpuMs(submit_start, submit_end),
                                            std::string(PROFILE_COUNTS, ','))
                              : std::string();
    return recorded;
  }

  ~RtScene12() {
#if defined(RENODX_RECORDER)
    if (recording_writer_.valid()) recording_writer_.wait();
#endif
    WaitForGpuBeforeRelease(own_fence_.Get(), event_, own_value_, FENCE_TIMEOUT, "ray tracing");
    if (event_ != nullptr) CloseHandle(event_);
    for (auto& baking : micromap_baking_) {
      if (baking.valid()) baking.wait();
    }
    if (placed_growing_.valid()) placed_growing_.wait();
#if defined(RENODX_NVAPI_CLUSTERS)
    if (cluster_growing_.valid()) cluster_growing_.wait();
#endif
#if defined(RENODX_OMM_SDK)
    for (const ommBaker baker : omm_.bakers) {
      if (baker != nullptr) omm_.destroy_baker(baker);
    }
    if (omm_.gpu_pipeline != nullptr) omm_.gpu_destroy_pipeline(omm_.gpu_baker, omm_.gpu_pipeline);
    if (omm_.gpu_baker != nullptr) omm_.destroy_baker(omm_.gpu_baker);
    // omm-lib.dll stays loaded: the CPU bakes' OpenMP threads idle in the runtime it brought, unloading it under them crashed
    // the helper at the second GPU reset (the first has no CPU bakes before it).
#endif
  }

 private:
  static constexpr UINT AGILITY_SDK_VERSION = 619;
  static constexpr GUID SDK_CONFIGURATION = {0x7cda6aca, 0xa03e, 0x49c8, {0x94, 0x58, 0x03, 0x34, 0xd2, 0x0e, 0x07, 0xce}};
  static constexpr GUID DRED_CONFIGURATION = {0x4a75bbc4, 0x9ff4, 0x4ad8, {0x9f, 0x18, 0xab, 0xae, 0x84, 0xdc, 0x5f, 0xf2}};
  static constexpr GUID DEBUG_CONFIGURATION = {0xf2352aeb, 0xdd84, 0x49fe, {0xb9, 0x7b, 0xa9, 0xdc, 0xfd, 0xcc, 0x1b, 0x4f}};  // CLSID_D3D12Debug
  // DRED's operations as LostReport names them (the ones recorded here).
  struct OperationName {
    D3D12_AUTO_BREADCRUMB_OP op;
    const char* name;
  };
  static constexpr std::array<OperationName, 12> OPERATION_NAMES = {{
      {D3D12_AUTO_BREADCRUMB_OP_SETMARKER, "marker"},
      {D3D12_AUTO_BREADCRUMB_OP_DISPATCH, "Dispatch"},
      {D3D12_AUTO_BREADCRUMB_OP_DISPATCHRAYS, "DispatchRays"},
      {D3D12_AUTO_BREADCRUMB_OP_BUILDRAYTRACINGACCELERATIONSTRUCTURE, "BuildAS"},
      {D3D12_AUTO_BREADCRUMB_OP_EMITRAYTRACINGACCELERATIONSTRUCTUREPOSTBUILDINFO, "EmitASInfo"},
      {D3D12_AUTO_BREADCRUMB_OP_COPYRAYTRACINGACCELERATIONSTRUCTURE, "CopyAS"},
      {D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION, "CopyBufferRegion"},
      {D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION, "CopyTextureRegion"},
      {D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER, "Barrier"},
      {D3D12_AUTO_BREADCRUMB_OP_RESOLVEQUERYDATA, "ResolveQueryData"},
      {D3D12_AUTO_BREADCRUMB_OP_EXECUTEINDIRECT, "ExecuteIndirect"},
      {D3D12_AUTO_BREADCRUMB_OP_EXECUTEEXTENSIONCOMMAND, "ExtensionCommand"},
  }};
  static constexpr size_t SLOTS = 3;
  // A slot's GPU timestamps: a trace's start, scene, TLAS, end, its clusters' start and end (a paths request's start and
  // end at 4, 5), then the profile's (RT_PROFILE) parts' ends: a trace's sun, AO, local lights (fog after); a paths
  // request's radiance cache (or the Light Bounce's rays), paths, reuse (reflections after).
  static constexpr size_t STAMPS = 10;
  static constexpr uint32_t TRACE_PART_SUN = 1u << 30;  // rt_trace.cs_6_5.hlsl PART_*
  static constexpr uint32_t TRACE_PART_AMBIENT = 1u << 31;
  // The CPU's instances at most, and the placed ones' room at first (32 MB of descs; grass past its share is placed, a
  // patch an instance: ATN's second profile placed up to 126 K; 524,288 since 262,144 ran out in ATN's 2026-11-01 stress
  // battle: 70-184 K placed a frame, the kept ones past the limit left out, others each frame: shadows flickering).
  static constexpr UINT MAX_INSTANCES = 524288;
  static constexpr UINT TRANSFER_VERTICES = dlaa_bridge::RT_TRANSFER_WIDTH * dlaa_bridge::RT_TRANSFER_HEIGHT;
  // Meshes drawn within this many frames cast (but the terrain only in frames that draw it); unseen longer: released.
  static constexpr uint32_t RECENT_FRAMES = 300;
  static constexpr uint32_t RELEASE_FRAMES = 3600;
  // The units' BLAS is refitted to each frame's poses while their draws stay the same, rebuilt this often.
  static constexpr uint32_t UNITS_REBUILD_FRAMES = 30;
  static constexpr UINT DYNAMIC_TRIANGLES = dlaa_bridge::RT_DYNAMIC_TRIANGLES;
  // Mega Geometry: the units' captured triangles in clusters of this many (three vertices each, up to 256 a cluster), a
  // frame's clusters at most (the compact draws' first; the last of each capture partial), each a geometry with a record
  // of its own (a slot's in turn).
  static constexpr uint32_t CLUSTER_TRIANGLES = dlaa_bridge::RT_CAPTURE_CLUSTER_TRIANGLES;
  static constexpr uint32_t MAX_CLUSTERS = dlaa_bridge::RT_DYNAMIC_CLUSTERS;
  // Mega Geometry's most a frame by the GPU's memory (~9.3 GB at all of it), a failed room's next.
  static constexpr std::array<uint32_t, 4> CLUSTER_TIERS = {MAX_CLUSTERS, MAX_CLUSTERS / 4 * 3, MAX_CLUSTERS / 4, MAX_CLUSTERS / 8};
  static constexpr uint32_t CLUSTER_ROOM = 32768;  // Mega Geometry's room for a frame's clusters at first (~200 MB)
  static constexpr uint32_t CLUSTER_STEP = 8192;   // its runs grow in steps of this
  // Compact draws (v16): their cluster sets' pool (bytes, in 16-byte units; past it a word the compact pass may read),
  // a slot's upload buffer for its draws' table (four uint4 each: rt_compact.cs_6_5.hlsl), then its new sets' data.
  static constexpr uint32_t SET_POOL_BYTES = 64u << 20;
  // Cluster templates (Mega Geometry): an opaque compact draw's clusters instantiated each frame from templates built once
  // per set (its triangles, no positions), a request's template builds at most (their arguments, 72 bytes each, and
  // destinations after the new sets' data), the templates' pool.
  static constexpr uint32_t TEMPLATE_BUILDS = 16384;
  static constexpr uint64_t TEMPLATE_POOL_BYTES = 128ull << 20;  // ~43,000 templates of 3 KB (a battle: ~700 sets)
  // The static BLASes' pools (BlasPlace): chunks of 32 MB in 256-byte units; a BLAS past 8 MB takes a buffer of its own.
  static constexpr uint64_t BLAS_CHUNK_BYTES = 32ull << 20;
  static constexpr uint32_t BLAS_CHUNK_UNITS = static_cast<uint32_t>(BLAS_CHUNK_BYTES / D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
  static constexpr uint64_t BLAS_OWN_BYTES = BLAS_CHUNK_BYTES / 4;
  static constexpr uint32_t BUILD_POOL = 0;
  static constexpr uint32_t KEPT_POOL = 1;
  static constexpr uint64_t COMPACT_SETS_OFFSET = uint64_t{dlaa_bridge::RT_COMPACT_DRAWS} * 64;
  static constexpr uint64_t COMPACT_TEMPLATE_ARGS_OFFSET = COMPACT_SETS_OFFSET + dlaa_bridge::RT_CLUSTER_DATA_BYTES;
  static constexpr uint64_t COMPACT_TEMPLATE_DESTINATIONS_OFFSET = COMPACT_TEMPLATE_ARGS_OFFSET + uint64_t{TEMPLATE_BUILDS} * 72;
  static constexpr uint64_t COMPACT_TEMPLATE_COUNT_OFFSET = COMPACT_TEMPLATE_DESTINATIONS_OFFSET + uint64_t{TEMPLATE_BUILDS} * 8;
  static constexpr uint64_t COMPACT_UPLOAD_BYTES = COMPACT_TEMPLATE_COUNT_OFFSET + 16;
  // A slot's upload buffer for the captures' clusters: the CLASes' arguments (64 bytes each, copied into cluster_args_
  // after the compact draws' built from triangles), the cluster BLASes' (16 each: the rest's before and after the
  // grass's, the grass's, the smoke's), the CLAS counts (built from triangles, from templates), the BLASes'
  // destinations, the indices (0-255), the clusters' records (by geometry index).
  static constexpr size_t CLUSTER_BLASES = 4;
  static constexpr uint64_t CLUSTER_ARGS_OFFSET = 0;
  static constexpr uint64_t CLUSTER_BLAS_ARGS_OFFSET = uint64_t{MAX_CLUSTERS} * 64;
  static constexpr uint64_t CLUSTER_COUNT_OFFSET = CLUSTER_BLAS_ARGS_OFFSET + (CLUSTER_BLASES * 16);
  // rt_sway.hlsli's flags; sway_counts_'s bytes (the cluster BLASes' arguments and the CLAS counts, as in that upload, then
  // rt_sway's own: its GROUPS, 16 bytes per 64 clusters).
  static constexpr uint32_t SWAY_CUT = 1;
  static constexpr uint32_t SWAY_IN_VIEW = 2;
  static constexpr uint64_t SWAY_COUNTS_BYTES = 96 + (uint64_t{MAX_CLUSTERS} / 64 * 16) + 16;
  static constexpr uint64_t CLUSTER_DESTINATION_OFFSET = CLUSTER_COUNT_OFFSET + 8;
  static constexpr uint64_t CLUSTER_BLAS_COUNT_OFFSET = CLUSTER_DESTINATION_OFFSET + (CLUSTER_BLASES * 8);  // the batched build's
  static constexpr uint64_t CLUSTER_INDICES_OFFSET = CLUSTER_DESTINATION_OFFSET + 248;
  static_assert(CLUSTER_BLAS_COUNT_OFFSET + 4 <= CLUSTER_INDICES_OFFSET);
  static constexpr uint64_t CLUSTER_RECORDS_OFFSET = CLUSTER_INDICES_OFFSET + 256;
  static constexpr uint64_t CLUSTER_UPLOAD_BYTES = CLUSTER_RECORDS_OFFSET + uint64_t{MAX_CLUSTERS} * 16;
  // The partitioned TLAS: an instance per static record and the units' last, this many a partition; a slot's upload
  // buffer: the write arguments (104 bytes each), the update arguments (16), the two operations, their count.
  static constexpr uint32_t PTLAS_INSTANCES = MAX_INSTANCES + 1;
  static constexpr uint32_t PTLAS_PARTITION_SIZE = 256;
  static constexpr uint64_t PTLAS_WRITES_OFFSET = 0;
  static constexpr uint64_t PTLAS_OPS_OFFSET = uint64_t{PTLAS_INSTANCES} * 104;
  static constexpr uint64_t PTLAS_COUNT_OFFSET = PTLAS_OPS_OFFSET + 64;
  static constexpr uint64_t PTLAS_UPLOAD_BYTES = PTLAS_COUNT_OFFSET + 16;
  // Records (InstanceID + geometry index): static meshes first, then each slot's units in turn, then each slot's
  // clusters; the pools: a normal per triangle, a UV per leaf vertex (static meshes first, then each slot's units).
  static constexpr UINT STATIC_RECORDS = 16384;
  // Instanced meshes' rows read back: a slot's request at most this many instances.
  static constexpr UINT ROWS_READBACK = 32768;
  // Placed meshes (rt_place.cs_6_5.hlsl): a request's jobs at most (its new captures', its placements'), after the
  // records' BLAS addresses in its upload buffer; the dedupe table's keys.
  static constexpr UINT PLACE_JOBS = dlaa_bridge::RT_CAPTURES + dlaa_bridge::RT_PLACEMENTS;
  static constexpr uint64_t PLACE_TABLE_BYTES = uint64_t{STATIC_RECORDS} * 8;
  static constexpr UINT CLUSTER_RECORDS = STATIC_RECORDS + SLOTS * dlaa_bridge::RT_CAPTURES;
  static constexpr UINT RECORD_COUNT = CLUSTER_RECORDS + SLOTS * MAX_CLUSTERS;
  static constexpr UINT STATIC_TRIANGLES = 8u << 20;
  static constexpr UINT NORMAL_POOL = STATIC_TRIANGLES + SLOTS * DYNAMIC_TRIANGLES;
  static constexpr UINT STATIC_UVS = 8u << 20;
  static constexpr UINT DYNAMIC_UVS = dlaa_bridge::RT_DYNAMIC_UVS;  // a slot's at most (grass, swaying leaves: dynamic_uvs_)
  static constexpr uint32_t RECORD_MASKED = 1;  // rt_scene.hlsli
  static constexpr uint32_t RECORD_THIN = 2;
  static constexpr uint32_t RECORD_SMOKE = 4;
  static constexpr uint32_t TRACE_SMOKE = 1u << 18;  // rt_scene.hlsli SMOKE_MET: the smoke's TLAS holds it
  static constexpr uint32_t RECORD_GRASS = 8;
  static constexpr uint32_t RECORD_LEAF = 16;
  // TLAS instance masks (rt_scene.hlsli; rt_place.cs_6_5.hlsl for the placed meshes): the smoke in an instance of its
  // own (the frame's last dynamic captures, a BLAS apart), which only the rays toward the sun meet; the leaves' meshes
  // their own group (Light Through Leaves counts them apart); everything else solid.
  static constexpr UINT SOLID_INSTANCE = 0x01;
  static constexpr UINT SMOKE_INSTANCE = 0x02;
  static constexpr UINT LEAF_INSTANCE = 0x04;
  static constexpr UINT GRASS_INSTANCE = 0x08;       // placed grass (most rays pass it)
  static constexpr UINT GRASS_SWAY_INSTANCE = 0x10;  // the swaying grass's cluster BLAS
  static constexpr UINT CUT_OUT_INSTANCE = 0x40;     // static solid cut-outs (rt_scene.hlsli CUT_OUTS)
  static constexpr float LEAF_ALPHA_CUTOFF = 0.5f;  // rt_scene_data.cs_6_5.hlsl
  static constexpr uint32_t PLACE_FOLIAGE = 1;  // rt_place.cs_6_5.hlsl
  static constexpr uint32_t PLACE_HIDDEN = 2;
  static constexpr uint32_t PLACE_GRASS = 4;
  static constexpr uint32_t PLACE_CUT_OUT = 8;
  // Opacity micromaps (NVIDIA's OMM SDK): a leaf mesh's data at most (the SDK makes micromaps coarser to fit), its
  // triangles at most, the bakes on their way at most.
  static constexpr uint32_t MICROMAP_MESH_BYTES = 8u << 20;
  static constexpr uint32_t MICROMAP_TRIANGLES = 1u << 18;
  static constexpr size_t MICROMAP_BAKES = 128;
  // The BLASes a request can build for compaction: a new one each capture, one with its micromaps each bake finished.
  static constexpr size_t COMPACTIONS = dlaa_bridge::RT_CAPTURES + MICROMAP_BAKES;
  static constexpr size_t MICROMAP_WORKERS = 4;  // bakes at once (each with the SDK's own threads)
  static constexpr size_t SET_MICROMAP_READS = 32;  // grass sets' micromap reads a request at most
  static constexpr uint64_t MICROMAP_WORKLOAD = 1ull << 28;  // a bake's texel tests at most (coarser micromaps past it)
  // GPU Micromap Baking: bakes a request at most and their triangles (the game waits for them: the rest in the next requests,
  // a battle's few hundred within a second or two), the SDK's descriptors (a ring: about 71 a bake), its scratch.
  static constexpr size_t MICROMAP_GPU_BAKES = 8;
  static constexpr uint32_t MICROMAP_GPU_TRIANGLES = 1u << 15;
  static constexpr UINT OMM_DESCRIPTORS = 65536;
  // Grass's finest micromap level (leaves': 6): new grass's shadows wait for its bakes (ATN's debug28: still a visible
  // delay), and level 6 is four times the work.
  static constexpr int GRASS_MICROMAP_LEVEL = 5;
  // The scene data pass's garbage count (rt_scene_data.cs_6_5.hlsl): three counts, the example's capture, its vertices.
  static constexpr UINT LEFT_OUT_BYTES = 64;
  // A request slower than this on the GPU is reported (ms), at most this many times.
  static constexpr double SLOW_MS = 100.0;
  static constexpr uint32_t SLOW_REPORTS = 50;
  // The effects' history (EnsureHistory): its kinds, released after this many frames unused (~10 s).
  enum History : size_t {
    HISTORY_PATHS,
    HISTORY_REUSE,
    HISTORY_SPECULAR,
    HISTORY_AMBIENT,
    HISTORY_LIGHTS,
    HISTORY_REFLECT,
    HISTORY_WATER,
    HISTORY_SKY,
    HISTORY_RATIO,
    HISTORY_KINDS
  };
  static constexpr uint32_t HISTORY_FRAMES = 600;
  // The descriptor table: SRVs t1-t7, UAVs u0-u5.
  enum Descriptor : UINT {
    POSITIONS,
    SUN,
    RECORDS,
    UVS,
    NORMALS,
    ATLAS,
    VERTICES,
    VISIBILITY,
    AMBIENT,
    SUN_RAYS,
    RECORDS_WRITTEN,
    UVS_WRITTEN,
    NORMALS_WRITTEN,
    LIGHT,   // t9 (the ray-traced Light Bounce's)
    BOUNCE,  // u9
    // Paths (v8): t10-t14, then u10-u21.
    PREVIOUS_COLOR,
    PREVIOUS_POSITIONS,
    GUIDE_NORMALS,
    GUIDE_ALBEDO,
    GUIDE_ROUGHNESS,
    CACHE_KEYS,
    CACHE_RADIANCE,
    CACHE_SUMS,
    CACHE_ALBEDO,
    LIGHT_FIT,
    LIGHT_KEPT,
    SPECULAR_ACCUMULATED,
    SPECULAR_HISTORY,
    SPECULAR_META,
    SPECULAR_META_HISTORY,
    REFLECTION,
    SPECULAR_OCCLUSION,
    LIGHT_MASK,  // u22 (v9)
    REUSE_SAMPLES,  // u23-u24: ReSTIR GI's reservoirs, two sets
    REUSE_DATA = REUSE_SAMPLES + 2,  // u25-u26
    FOG = REUSE_DATA + 2,  // u27 (v11)
    AMBIENT_HISTORY,  // u28-u29: the ambient visibility kept over frames, two sets (v11)
    AMBIENT_POSITIONS = AMBIENT_HISTORY + 2,  // u30: the last trace's positions
    HIT_DISTANCE,  // u31: Ray Reconstruction's specular hit distance (v17)
    LIGHT_HISTORY,  // u32-u33: the local lights' bits kept over frames, two sets
    FOG_LIGHTS = LIGHT_HISTORY + 2,  // u34 (v18): the local lights' light in the fog
    TINT,  // u35 (v19): Coloured Shadows' filter
    REFLECT_SAMPLES,  // u36-u37 (v19, helpers 55 on): Own ReSTIR's reservoirs, two sets
    REFLECT_DATA = REFLECT_SAMPLES + 2,  // u38-u39
    // NRD's inputs (helpers 73 on; rt_nrd_front.hlsl): u40-u43 the sun's, u44-u47 the Light Bounce's, u48-u50 the
    // reflections', u51-u53 the ambient's.
    NRD_SUN_VIEW_Z = REFLECT_DATA + 2,
    NRD_SUN_NORMAL_ROUGHNESS,
    NRD_SUN_PENUMBRA,
    NRD_SUN_TRANSLUCENCY,
    NRD_BOUNCE_HIT_DISTANCE,
    NRD_BOUNCE_VIEW_Z,
    NRD_BOUNCE_NORMAL_ROUGHNESS,
    NRD_BOUNCE_SIGNAL,
    NRD_REFLECT_VIEW_Z,
    NRD_REFLECT_NORMAL_ROUGHNESS,
    NRD_REFLECT_SIGNAL,
    NRD_AMBIENT_VIEW_Z,
    NRD_AMBIENT_NORMAL_ROUGHNESS,
    NRD_AMBIENT_SIGNAL,
    OWN_SKY,  // u54-u55 (helpers 107 on): Path-Traced Ambient's own sky at the bounce texels, two sets
    LIGHT_RATIO = OWN_SKY + 2,  // u56-u57 (helpers 111 on): Torch and Fire Shadows from the Light Bounce's ratio, two sets
    SURFACE_ORIGIN = LIGHT_RATIO + 2,  // u58: ReSTIR GI's bounce texels' surfaces as this frame's paths found them
    SURFACE_NORMAL,                    // u59
    // The reference build's (rt_reference.hlsli u60-u72): the candidate's batch sums, the reference's group sums, the
    // run's state; the table twice in the heap (the second, the frozen copies', for the reference's slices). The slot rule
    // (PT DI step 2): one enum in both builds, every new slot after REF_STATE at a fixed register, null where unused
    // (WriteNullPathDescriptors).
    REF_CANDIDATE_BATCH,
    REF_CANDIDATE_SHIFT,
    REF_CANDIDATE_MEAN,
    REF_CANDIDATE_M2,
    REF_CANDIDATE_CROSS,
    REF_CANDIDATE_PREVIOUS,
    REF_CANDIDATE_LAST,
    REF_GROUP_SUM,
    REF_MEAN,
    REF_M2,
    REF_GROUPS,
    REF_AUX,
    REF_STATE,
    RTXDI_GI_RESERVOIRS,  // u73: the reference build's RTXDI GI contest's reservoirs (rt_path.hlsli rtxdi_gi_reservoirs)
    RTXDI_OFFSETS,        // u74: its neighbour offsets
    TORCH_IRRADIANCE,     // u75: Torch Light in Path-Traced GI's torch light at the render pixels (rt_scene.hlsli torch_irradiance)
    REGIR_CELLS,          // u76: its ReGIR cells (rt_rtxdi_bridge.hlsli regir_cells)
    DESCRIPTOR_COUNT,
  };
  // The registers (the last UAV range starts at u10 with CACHE_KEYS).
  static_assert(REF_CANDIDATE_BATCH - CACHE_KEYS + 10 == 60 && REF_STATE - CACHE_KEYS + 10 == 72);
  static_assert(RTXDI_GI_RESERVOIRS - CACHE_KEYS + 10 == 73);
  static_assert(RTXDI_OFFSETS - CACHE_KEYS + 10 == 74);
  static_assert(TORCH_IRRADIANCE - CACHE_KEYS + 10 == 75);
  static_assert(REGIR_CELLS - CACHE_KEYS + 10 == 76);
#if defined(RENODX_REFERENCE)
  static constexpr UINT TABLES = 2;  // the live table, the reference's frozen one
#else
  static constexpr UINT TABLES = 1;
#endif
  static constexpr UINT SRV_COUNT = VISIBILITY;
  // rt_path.hlsli's path flags and cache size.
  static constexpr uint32_t PATH_TRACING = 1;
  static constexpr uint32_t PATH_REFLECTIONS = 2;
  static constexpr uint32_t PATH_RAW = 512;
  static constexpr uint32_t PATH_NRD = 268435456;
  static constexpr uint32_t PATH_SQUARED_LOBE = 536870912;  // rt_camera.hlsli
  static constexpr uint32_t PATH_REFLECT_FOG = 16384;
  static constexpr uint32_t PATH_REFLECT_STEADY = 32768;  // rt_reflect_filter (the trace's PATH_AMBIENT_PARITY: another request's word)
  static constexpr uint32_t TRACE_NRD_BOUNCE = 4194304;   // rt_scene.hlsli
  static constexpr uint32_t TRACE_NRD_AMBIENT = 8388608;  // rt_scene.hlsli
  static constexpr uint32_t PATH_SPECULAR_OCCLUSION = 4;
  static constexpr uint32_t PATH_HISTORY_VALID = 8;
  static constexpr uint32_t PATH_CACHE_VALID = 16;
  static constexpr uint32_t PATH_SPECULAR_HISTORY = 32;
  static constexpr uint32_t PATH_GUIDES = 64;
  static constexpr uint32_t PATH_SUN_TRACED = 128;
  static constexpr uint32_t PATH_LIGHT = 256;
  static constexpr uint32_t PATH_REUSE = 1024;
  static constexpr uint32_t PATH_REUSE_HISTORY = 2048;
  static constexpr uint32_t PATH_REUSE_PARITY = 4096;
  static constexpr uint32_t PATH_REFLECT_REUSE = 4194304;
  static constexpr uint32_t PATH_REFLECT_OWN = 8388608;
  static constexpr uint32_t PATH_REFLECT_OWN_HISTORY = 16777216;
  static constexpr uint32_t PATH_REFLECT_PARITY = 33554432;
  static constexpr uint32_t PATH_LIGHT_FINAL = 67108864;
  static constexpr uint32_t PATH_SKY_APART = 134217728;
  static constexpr uint32_t PATH_LOCAL_LIGHTS = 8192;
  static constexpr uint32_t PATH_AMBIENT_HISTORY = 16384;  // rt_camera.hlsli
  static constexpr uint32_t PATH_AMBIENT_PARITY = 32768;
  static constexpr uint32_t PATH_TINT = 1048576;  // rt_camera.hlsli
  static constexpr uint32_t PATH_TINT_GUIDES = 2097152;
  static constexpr uint32_t PATH_AMBIENT_KEPT = 65536;  // rt_camera.hlsli (the paths' PATH_PERMUTE: another request's word)
  static constexpr uint32_t PATH_LIGHT_HISTORY = 131072;
  static constexpr uint32_t PATH_PERMUTE = 65536;
  static constexpr uint32_t PATH_AMBIENT = 262144;  // rt_path.hlsli
  static constexpr uint32_t PATH_KEEP_LIGHT = 524288;
  static constexpr uint32_t PATH_KEEP_CUBE = 1073741824;
  static constexpr uint32_t PATH_SKY_HISTORY = 131072;  // rt_path.hlsli (the trace's PATH_LIGHT_HISTORY: another request's word)
  static constexpr uint32_t PATH_SKY_PARITY = 1048576;  // rt_path.hlsli (the trace's PATH_TINT)
  static constexpr uint32_t PATH_SUN_LIGHT = 2097152;   // rt_path.hlsli (the trace's PATH_TINT_GUIDES)
  static constexpr uint32_t PATH_START_PAST = 2147483648;  // rt_camera.hlsli
  // Read only by the REUSE_TESTS builds.
  static constexpr uint32_t REUSE_SUSPECT_BITS = REUSE_TEST_SPATIAL_CLAMP | REUSE_TEST_SKY_DIRECTION | REUSE_TEST_NO_JACOBIAN_CUT | REUSE_TEST_HISTORY_PLAIN
                                                 | REUSE_TEST_NEIGHBOUR_VISIBILITY | REUSE_TEST_JACOBIAN_SATURATE | REUSE_BIAS_BASIC | REUSE_BIAS_RAYTRACED
                                                 | REUSE_TEST_VALIDATE_NO_DROP | REUSE_TEST_VALIDATE_KEEP_LIGHT | REUSE_TEST_VALIDATE_NO_CUTS
                                                 | REUSE_TEST_VALIDATE_RESCALE | REUSE_TEST_VALIDATE_NO_INVALIDITY
                                                 | REUSE_TEST_VALIDATE_KEEP_CHROMA | REUSE_TEST_NO_HISTORY_CLAMP | REUSE_TEST_VALIDATE_NO_CACHE
                                                 | REUSE_BIAS_PAIRWISE;
  static constexpr UINT RTXDI_RESERVOIR_BYTES = 32;  // RTXDI_PackedGIReservoir
  static constexpr UINT RTXDI_OFFSET_COUNT = 1024;   // rt_path.hlsli RTXDI_NEIGHBOUR_OFFSETS
  static constexpr uint32_t LIGHTS_IN_PATHS = 128;  // rt_camera.hlsli (PathConstants::lights_in_bounce)
  static constexpr uint32_t LIGHTS_RATIO_BITS = 256;  // rt_camera.hlsli: a trace's, the addon reads flagged bits (RT_SCENE_LIGHT_BITS)
  static constexpr uint32_t LIGHTS_REGIR = 512;              // rt_scene.hlsli: a paths request's ReGIR cells built
  static constexpr uint32_t LIGHTS_TORCH_IRRADIANCE = 1024;  // rt_scene.hlsli: rt_lights' torch light (a trace's: written)
  static constexpr UINT LIGHT_KEPT_TEXELS = 5;  // rt_path.hlsli light_kept
  static constexpr UINT CACHE_ENTRIES = 1u << 20;
  // rt_path.hlsli's b3.
  struct PathConstants {
    std::array<float, 16> view_projection = {};
    std::array<float, 16> previous_view_projection = {};
    uint32_t bounce_width = 0;
    uint32_t bounce_height = 0;
    uint32_t bounce_scale = 1;
    uint32_t flags = 0;
    uint32_t bounces = 0;
    uint32_t samples = 1;
    float bounce_range = 10.f;
    float reflection_range = 10.f;
    float sky_range = 2.f;  // rt_camera.hlsli
    uint32_t previous_square_root = 0;  // RT_PATH_COLOR_SQUARE_ROOT
    uint32_t previous_positions_slice = 0;  // kept_positions_' slice with last frame's
    uint32_t lights_in_bounce = 0;  // rt_camera.hlsli LIGHTS_*: the mode (RT_PATH_LIGHTS_IN_BOUNCE 1, with RT_PATH_LIGHT_RATIO 3), the ratio's set and validity
    uint32_t reuse_tests = 0;  // REUSE_TEST_* (tests, and RT_PATH_REUSE_BALANCED's kept light; 0: none)
#if defined(RENODX_PATH_PROBE)
    uint32_t probe_mode = 0;  // RENODX_TW_PATH_PROBE: rt_path.hlsli ProbeTexel's mode | its rays a texel << 8
#endif
  };
  // Torch and Fire Light in the Light Bounce for the harness without an addon asking (RENODX_TW_LIGHTS_IN_BOUNCE: 1 as asked,
  // 2 the bounce holding only the lights' light: a measurement, 3 Torch and Fire Shadows from the Light Bounce); 0 none.
  static uint32_t LightsInBounceTest() {
    std::array<wchar_t, 4> value = {};
    if (GetEnvironmentVariableW(L"RENODX_TW_LIGHTS_IN_BOUNCE", value.data(), static_cast<DWORD>(value.size())) == 0) return 0;
    switch (value[0]) {
      case L'2':
        return 2u;
      case L'3':
        return 3u;
      default:
        return 1u;
    }
  }
#if defined(RENODX_NRD)
  // NRD's texture of a signal (nrd_d3d12.hpp), with a place in the trace's descriptors when rt_nrd_front.hlsl writes it (0: none).
  struct NrdTexture {
    nrd::ResourceType type;
    DXGI_FORMAT format;
    UINT descriptor;
  };
  struct NrdSignal {
    std::unique_ptr<Nrd12> denoiser;
    ComPtr<ID3D12PipelineState> front;
    std::array<uint32_t, 2> size = {};
    uint32_t used_frame = 0;
    bool failed = false;
  };
  static constexpr uint32_t NRD_RELEASE_FRAMES = 120;
  static constexpr D3D12_SHADER_RESOURCE_VIEW_DESC POSITIONS_VIEW = {.Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                                                                     .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING, .Texture2D = {.MipLevels = 1}};
#endif
  enum Root : UINT {
    ROOT_SCENE,
    ROOT_TABLE,
    ROOT_SETTINGS,
    ROOT_DISPATCH,
    ROOT_CAPTURE_TABLE,
    ROOT_MICROMAP_DATA,
    ROOT_MICROMAP_INDICES,
    ROOT_MICROMAP_DESCS,
    ROOT_BOUNCE,  // b2: the ray-traced Light Bounce's constants
    ROOT_PATHS,   // b3: the paths' constants (rt_path.hlsli)
    ROOT_LIGHTS,  // b4: the trace's local lights
    ROOT_SMOKE,   // t15: the smoke's TLAS (rt_scene.hlsli smoke_scene)
    ROOT_SCRATCH,  // u0 space1: a pass's own scratch (rt_place's dedupe: PlacedRoom::scratch; the cache fill's owners)
  };
  // b4 (rt_scene.hlsli Lights): RT_ALL_LIGHTS of world position and reach, their count, their colours, their places in
  // the last trace's list and their falloffs (a byte each); a CBV's 256-byte multiple.
  static constexpr UINT LIGHT_COUNT_OFFSET = dlaa_bridge::RT_ALL_LIGHTS * 16;
  static constexpr UINT LIGHT_COLOURS_OFFSET = LIGHT_COUNT_OFFSET + 16;
  static constexpr UINT LIGHT_PREVIOUS_OFFSET = LIGHT_COLOURS_OFFSET + (dlaa_bridge::RT_ALL_LIGHTS * 16);
  static constexpr UINT LIGHT_KINDS_OFFSET = LIGHT_PREVIOUS_OFFSET + dlaa_bridge::RT_ALL_LIGHTS;
#if defined(RENODX_REGIR)
  // Then a paths request's ReGIR onion (rt_scene.hlsli Lights::regir, the PATH_REGIR builds; its next 16-byte register).
  static constexpr UINT REGIR_OFFSET = LIGHT_KINDS_OFFSET + dlaa_bridge::RT_ALL_LIGHTS;
  static_assert(REGIR_OFFSET % 16 == 0 && sizeof(ReGIR_Parameters) % 16 == 0);
  static constexpr UINT LIGHT_CONSTANT_BYTES = (REGIR_OFFSET + sizeof(ReGIR_Parameters) + 255) / 256 * 256;
  // RTXDI's onion with its default layers (5 detail, 10 coverage: ~2100 cells out to ~144 cells from the centre): cells of
  // REGIR_CELL_SIZE metres at the camera (~290 m out; past it every light picked uniformly), REGIR_LIGHTS_PER_CELL picks a
  // cell (slots past the onion write nothing) of REGIR_BUILD_SAMPLES uniform candidates each (RTXDI's default), no jitter (a
  // surface takes the cell it lies in, which every light it gets light from reaches: RAB_GetLightTargetPdfForVolume).
  static constexpr float REGIR_CELL_SIZE = 2.f;
  static constexpr uint32_t REGIR_LIGHTS_PER_CELL = 16;
  static constexpr uint32_t REGIR_BUILD_SAMPLES = 8;
#else
  static constexpr UINT LIGHT_CONSTANT_BYTES = (LIGHT_KINDS_OFFSET + dlaa_bridge::RT_ALL_LIGHTS + 255) / 256 * 256;
#endif

  // The addon's lights: at most RT_MAX_LIGHTS before v31; light `index`'s position and reach (or colour), the first
  // RT_MAX_LIGHTS in rt_lights, the others in rt_lights_more.
  static uint32_t SharedLightCount(const dlaa_bridge::Shared& shared, uint32_t version) {
    return std::min(shared.rt_light_count, (version >= 31) ? dlaa_bridge::RT_ALL_LIGHTS : dlaa_bridge::RT_MAX_LIGHTS);
  }
  static const float* SharedLight(const dlaa_bridge::Shared& shared, uint32_t index, bool colour) {
    if (index < dlaa_bridge::RT_MAX_LIGHTS) return colour ? shared.rt_light_colours[index] : shared.rt_lights[index];
    const uint32_t more = index - dlaa_bridge::RT_MAX_LIGHTS;
    return colour ? shared.rt_light_colours_more[more] : shared.rt_lights_more[more];
  }
  // b4's positions, count, colours and falloffs (an addon before v32 sends none: every light smooth).
  static void CopyLights(const dlaa_bridge::Shared& shared, uint32_t version, uint32_t count, uint8_t* data) {
    for (uint32_t light = 0; light < count; ++light) {
      std::memcpy(data + (light * 16), SharedLight(shared, light, false), 16);
      std::memcpy(data + LIGHT_COLOURS_OFFSET + (light * 16), SharedLight(shared, light, true), 16);
      data[LIGHT_KINDS_OFFSET + light] = (version >= 32 ? shared.rt_light_kinds[light] : dlaa_bridge::RT_LIGHT_SMOOTH);
    }
    std::memcpy(data + LIGHT_COUNT_OFFSET, &count, sizeof(count));
  }

  struct Slot {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList4> list;
    // Its last request and passes (LostReport).
    std::string request;
    std::vector<const wchar_t*> marks;
    std::string micromap_builds;  // LostReport: the BLASes built with micromaps (debug34: the GPU hung in one's build)
    bool stamped = false;  // its last request wrote GPU times (Resolve)
    uint32_t gpu_bakes = 0;  // GPU Micromap Baking's bakes in its last trace (the profile)
    ComPtr<ID3D12Resource> instances;
    D3D12_RAYTRACING_INSTANCE_DESC* instance_data = nullptr;
    ComPtr<ID3D12Resource> table;  // the scene data pass's capture table
    uint32_t* table_data = nullptr;
    // The compacted sizes of the BLASes it built (written by the builds, copied for the CPU), their meshes and builds.
    ComPtr<ID3D12Resource> sizes;
    ComPtr<ID3D12Resource> readback;
    std::vector<std::pair<uint64_t, uint64_t>> compactions;
    ComPtr<ID3D12Resource> paths;  // its paths' constants (b3)
    uint8_t* paths_data = nullptr;
    std::string profile_parts;  // the profile line's last field (a trace's record by pass)
    ComPtr<ID3D12Resource> lights;  // its trace's local lights (b4: rt_trace.cs_6_5.hlsl)
    // Instanced captures' rows copied for the CPU (read when the slot comes round): mesh key, its build, where, how many.
    ComPtr<ID3D12Resource> rows_readback;
    struct RowRead {
      uint64_t key = 0;
      uint64_t build = 0;
      uint32_t offset = 0;
      uint32_t count = 0;
    };
    std::vector<RowRead> row_reads;
    // The scene data pass's garbage left out (far off, stretched parts, stretched others, the first such capture's
    // index), read when the slot comes round, with that request's capture keys.
    ComPtr<ID3D12Resource> left_out;
    ComPtr<ID3D12Resource> left_out_readback;
    uint32_t* left_out_data = nullptr;
#if defined(RENODX_NRD)
    // The sun texture's first texel (toward the sun) copied for the CPU in a trace NRD's SIGMA ran in, read when the slot
    // comes round (SigmaSettings::lightDirection).
    ComPtr<ID3D12Resource> sun_readback;
    float* sun_data = nullptr;
    bool sun_copied = false;
#endif
    std::vector<uint64_t> capture_keys;
    std::vector<uint32_t> capture_flags;
    std::string status;  // its request's (a slow one's report)
    std::string profile;  // its request's profile line but its GPU times (WriteProfile); empty: none
    ComPtr<ID3D12Resource> jobs;  // the records' BLAS addresses and its placement jobs (rt_place's t8)
    uint8_t* jobs_data = nullptr;
    uint8_t* light_data = nullptr;
    ComPtr<ID3D12Resource> clusters;  // Mega Geometry's arguments and records (CLUSTER_*_OFFSET)
    uint8_t* cluster_data = nullptr;
    ComPtr<ID3D12Resource> compact;  // its compact draws' table and new cluster sets (COMPACT_*)
    uint8_t* compact_data = nullptr;
    ComPtr<ID3D12Resource> ptlas;  // the partitioned TLAS's operations (PTLAS_*_OFFSET)
    ComPtr<ID3D12Resource> ptlas_device;  // and their copy the build reads (device memory)
    uint8_t* ptlas_data = nullptr;
    uint64_t value = 0;  // own_fence_'s value after its last use
#if defined(RENODX_RECORDER)
    // The recorder's journal: its last trace's new static meshes' vertices copied for the CPU (TakeJournalReads), each
    // mesh's key, build, place in the read-back and vertices.
    struct JournalRead {
      uint64_t key = 0;
      uint64_t build = 0;
      uint64_t offset = 0;
      uint32_t vertices = 0;
    };
    std::vector<JournalRead> journal_reads;
    ComPtr<ID3D12Resource> journal_readback;
#endif
  };
  // A static BLAS's memory (suballocated as NVIDIA's RTXMU does: a committed buffer each cost 64 KB at least and a driver
  // allocation, hundreds of new meshes at once a hitch): a place in one of the pools' chunks (BLAS_CHUNK_BYTES buffers,
  // 256-byte units: the builds' and the compacted BLASes' pools, so short-lived builds leave no holes among the kept), or
  // a buffer of its own past BLAS_OWN_BYTES.
  struct BlasPlace {
    ComPtr<ID3D12Resource> own;
    uint32_t pool = 0;
    uint32_t chunk = 0;
    uint32_t offset = 0;  // units
    uint32_t units = 0;   // 0: none (or its own)
    uint64_t address = 0;  // GPU (0: none)
  };
  struct Mesh {
    BlasPlace blas;
    ComPtr<ID3D12Resource> micromaps;  // its opacity micromap array (DXR 1.2), kept while the BLAS lives
    uint64_t build = 0;
    uint64_t bytes = 0;
    uint32_t drawn_frame = 0;
    uint32_t triangles = 0;
    uint32_t record = 0;
    uint32_t normal_offset = 0;
    uint32_t uv_offset = UINT32_MAX;
    bool terrain = false;
    bool foliage = false;
    bool grass = false;  // RT_GRASS: its instances GRASS_INSTANCE
    bool solid_cut_out = false;  // RT_SOLID_CUT_OUT: alpha-tested in its own group (CUT_OUT_INSTANCE), no light through it
    // Instanced (RT_INSTANCED): each instance's transform from the first one's space (its BLAS's), read back from its
    // capture; their places (instance indices, the partitioned TLAS's) from first_instance on.
    std::vector<std::array<float, 12>> transforms;
    uint32_t first_instance = 0;
    // Placed (v14, RT_INSTANCED): in the scene only where its draws put its instances (rt_place), its build rows kept by
    // record.
    bool placed = false;
    bool moved = false;  // ever drawn moving (rt_place's MOVED_KEPT_FRAMES)
  };
  // A buffer, or a BLAS's place in a pool (`units` past 0), let go once the GPU is past request `value`.
  struct Release {
    uint64_t value = 0;
    ComPtr<ID3D12Resource> resource;
    uint32_t pool = 0;
    uint32_t chunk = 0;
    uint32_t offset = 0;
    uint32_t units = 0;
  };
  // A new leaf mesh's micromaps on their way: its UVs and opacity mask (its atlas slice) read back from the request that
  // built its BLAS, its positions kept; baked on a thread (NVIDIA's OMM SDK), then built into a micromap array with its
  // BLAS again (AdvanceMicromaps, Record).
  struct MicromapBake {
    uint64_t key = 0;
    uint64_t build = 0;  // its mesh's or its cluster set's generation (another: dropped)
    uint64_t value = 0;  // the reading request's
    uint32_t triangles = 0;
    bool set = false;  // a cluster set's (grass: its templates built with the micromaps), not a mesh's
    bool grass = false;  // GRASS_MICROMAP_LEVEL
    float cutoff = LEAF_ALPHA_CUTOFF;  // its draw's alpha cut-off (RT_OWN_CUTOFF's own)
    // Its request's buffers (one of each for all its reads) and its places in them: its vertices (16 bytes each) for the
    // rebuild; read back: a packed half UV a corner, its atlas slice.
    ComPtr<ID3D12Resource> positions;
    uint64_t positions_offset = 0;
    ComPtr<ID3D12Resource> uvs;
    uint64_t uvs_offset = 0;
    ComPtr<ID3D12Resource> mask;
    uint64_t mask_offset = 0;
    // GPU Micromap Baking's (BakeMicromapsOnGpu): its UVs copied into `uvs` (a GPU buffer then) and its mask into
    // OmmGpu::masks[mask_index] when read (`value` UINT64_MAX while it waits for a request's bakes); its outputs' buffers
    // (its request's bakes share them) and places, its histogram's place in their read-back (gpu_failed: refused).
    bool gpu = false;
    bool gpu_failed = false;
    uint32_t mask_index = 0;
    ComPtr<ID3D12Resource> gpu_arrays;
    uint64_t arrays_offset = 0;
    ComPtr<ID3D12Resource> gpu_descs;
    uint64_t descs_offset = 0;
    ComPtr<ID3D12Resource> gpu_indices;
    uint64_t indices_offset = 0;
    ComPtr<ID3D12Resource> gpu_readback;
    uint64_t histogram_offset = 0;
    uint32_t histogram_bytes = 0;
#if defined(RENODX_RECORDER)
    // The recorder's journal of a GPU bake: its outputs read back too, their places there (post-dispatch info, data,
    // descs, indices) and the data's and descs' room.
    ComPtr<ID3D12Resource> journal_readback;
    std::array<uint64_t, 4> journal_offsets = {};
    std::array<uint64_t, 2> journal_limits = {};
#endif
    std::vector<uint32_t> uv_data;
    std::vector<uint8_t> mask_data;
    // The bake's: the micromaps' data, descs, an index a triangle (32-bit; the special ones negative), their histogram.
    std::vector<uint8_t> data;
    std::vector<D3D12_RAYTRACING_OPACITY_MICROMAP_DESC> descs;
    std::vector<uint32_t> indices;
    std::vector<D3D12_RAYTRACING_OPACITY_MICROMAP_HISTOGRAM_ENTRY> histogram;
  };
#if defined(RENODX_OMM_SDK)
  // The OMM SDK's CPU baker and the calls the bakes use (omm-lib.dll's).
  struct OmmSdk {
    HMODULE library = nullptr;
    std::array<ommBaker, MICROMAP_WORKERS> bakers = {};  // a worker's each
    decltype(&ommDestroyBaker) destroy_baker = nullptr;
    decltype(&ommCpuCreateTexture) create_texture = nullptr;
    decltype(&ommCpuDestroyTexture) destroy_texture = nullptr;
    decltype(&ommCpuBake) bake = nullptr;
    decltype(&ommCpuGetBakeResultDesc) result_desc = nullptr;
    decltype(&ommCpuDestroyBakeResult) destroy_result = nullptr;
    // GPU Micromap Baking: the GPU baker, its pipeline and calls.
    ommBaker gpu_baker = nullptr;
    ommGpuPipeline gpu_pipeline = nullptr;
    decltype(&ommGpuCreatePipeline) gpu_create_pipeline = nullptr;
    decltype(&ommGpuDestroyPipeline) gpu_destroy_pipeline = nullptr;
    decltype(&ommGpuGetPipelineDesc) gpu_pipeline_desc = nullptr;
    decltype(&ommGpuGetPreDispatchInfo) gpu_pre_dispatch = nullptr;
    decltype(&ommGpuDispatch) gpu_dispatch = nullptr;
    decltype(&ommGpuGetStaticResourceData) gpu_static_data = nullptr;
  };
  // GPU Micromap Baking (MakeGpuBaker, BakeMicromapsOnGpu): the OMM SDK's compute pipelines (root signatures from its
  // descriptions: a table of its ranges, the global constants' CBV, its own root constants, its static samplers; the
  // raster ones are not made: bakes run ComputeOnly), its descriptors (a ring), the indirect dispatch's signature, the
  // corners' indices (ours are not indexed: 0, 1, 2...), its static buffers, the scratch it asks for (kept, grown), the
  // masks' copies (an atlas slice each: its alpha texture) and the bakes' clamped UVs.
  struct OmmGpu {
    struct Pipeline {
      ComPtr<ID3D12RootSignature> root;
      ComPtr<ID3D12PipelineState> state;
      int table = -1;     // its root parameters (-1: none)
      int constants = -1;
      int global = -1;
      UINT constant_count = 0;
    };
    std::vector<Pipeline> pipelines;
    uint32_t globals_bytes = 0;  // the global constants' largest size
    ComPtr<ID3D12DescriptorHeap> heap;
    UINT next = 0;
    ComPtr<ID3D12CommandSignature> dispatch;
    ComPtr<ID3D12Resource> indices;
    std::array<ComPtr<ID3D12Resource>, 2> statics;  // its static vertex and index buffers
    std::array<ComPtr<ID3D12Resource>, OMM_MAX_TRANSIENT_POOL_BUFFERS> transient;
    std::vector<ComPtr<ID3D12Resource>> masks;  // resting in NON_PIXEL_SHADER_RESOURCE
    ComPtr<ID3D12Resource> uvs;                 // resting in NON_PIXEL_SHADER_RESOURCE
    ComPtr<ID3D12PipelineState> clamp;          // rt_omm_uvs
  };
#endif
  // A pool's free ranges (offset -> count), first fit (as Tales of Arise's rt_shadows.hpp).
  struct Ranges {
    std::map<uint32_t, uint32_t> free;

    bool Take(uint32_t count, uint32_t* offset) {
      for (auto range = free.begin(); range != free.end(); ++range) {
        if (range->second < count) continue;
        *offset = range->first;
        const uint32_t left = range->second - count;
        free.erase(range);
        if (left != 0) free.emplace(*offset + count, left);
        return true;
      }
      return false;
    }

    void Give(uint32_t offset, uint32_t count) {
      auto next = free.lower_bound(offset);
      if (next != free.end() && offset + count == next->first) {
        count += next->second;
        next = free.erase(next);
      }
      if (next != free.begin()) {
        auto previous = std::prev(next);
        if (previous->first + previous->second == offset) {
          previous->second += count;
          return;
        }
      }
      free.emplace(offset, count);
    }
  };
  // Released meshes' records and pool ranges, given back once the GPU is past the requests that read them.
  struct PendingFree {
    uint64_t value = 0;
    uint32_t record = 0;
    uint32_t normal_offset = 0;
    uint32_t triangles = 0;
    uint32_t uv_offset = UINT32_MAX;
    uint32_t first_instance = 0;  // its instances' places (instanced meshes)
    uint32_t instances = 0;
  };
  // A compact draws' cluster set (v16): its data's place in set_pool_ (16-byte units), its block's vertices, its clusters
  // (first vertex, vertices, first triangle, triangles each) and triangles; its templates' first place in templates_
  // (UINT32_MAX: none), a template each cluster, alpha-tested or opaque.
  struct ClusterSet {
    uint32_t offset = 0;
    uint32_t units = 0;
    uint32_t vertices = 0;
    uint32_t clusters = 0;
    uint32_t triangles = 0;
    std::vector<std::array<uint32_t, 4>> descs;
    uint32_t template_first = UINT32_MAX;
    bool template_masked = false;
    uint64_t generation = 0;  // which set this is under its key (a bake for an older one is dropped)
    // Its opacity micromaps (alpha-tested sets, grass: baked once like the leaves'): the array and the upload holding a
    // triangle's index each at `micromap_indices`; its templates are built with them. `micromaps_tried`: read once.
    ComPtr<ID3D12Resource> micromaps;
    ComPtr<ID3D12Resource> micromap_inputs;
    uint64_t micromap_indices = 0;
    bool micromaps_tried = false;
  };
  // A replaced or dropped set's units, templates and micromaps, given back once the GPU is past the requests that read
  // them.
  struct PendingSetFree {
    uint64_t value = 0;
    uint32_t offset = 0;
    uint32_t units = 0;
    uint32_t template_first = UINT32_MAX;
    uint32_t templates = 0;
    ComPtr<ID3D12Resource> micromaps;
    ComPtr<ID3D12Resource> micromap_inputs;
  };
  // A micromap bake's read: a new leaf mesh's (its kept positions too) or an alpha-tested cluster set's (its first
  // instance's UVs this request, in its templates' triangle order).
  struct MicromapRead {
    uint64_t key = 0;
    uint64_t build = 0;
    uint32_t first_vertex = 0;
    uint32_t triangles = 0;
    uint32_t uv_offset = 0;
    uint32_t slice = 0;
    bool set = false;
    bool grass = false;
    float cutoff = LEAF_ALPHA_CUTOFF;
    bool gpu = false;  // baked on the GPU (BakeMicromapsOnGpu), else read back for the CPU
  };

  bool Fail(std::string status) {
    failed_ = true;
    status_ = std::move(status);
    return false;
  }

  static constexpr DWORD FENCE_TIMEOUT = 5000;

  // False: this queue's work up to `value` is not proven done on a live device (WaitForGpu). Nothing may be recorded
  // over it or freed under it: the scene counts as lost (no request records, main.cpp makes a new scene).
  bool WaitFor(uint64_t value) {
    if (WaitForGpu(own_fence_.Get(), event_, value, FENCE_TIMEOUT)) return true;
    timed_out_ = true;
    request_failed_ = true;
    stall_ = {.completed = own_fence_->GetCompletedValue(), .wanted = value};
    return false;
  }

  D3D12_CPU_DESCRIPTOR_HANDLE Descriptor(UINT index) const {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = descriptors_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += index * descriptor_size_;
    return handle;
  }

  static constexpr uint64_t Align(uint64_t value, uint64_t alignment) { return (value + alignment - 1) / alignment * alignment; }

  // `buffer` (default heap, UAV-capable) of `bytes` at least (in 64 KB steps; the old one released once the GPU is past
  // this request). False: no memory for it. Pass std::addressof(member): WRL's ComPtr operator& hands out a ComPtr* only
  // after releasing what it holds (ComPtrRef's operator T*), so GrowBuffer(&member, ...) made a new buffer at every call
  // (helper107-108: the sway's three, 16 MiB, every frame) and released the old one at once instead of after the GPU.
  bool GrowBuffer(ComPtr<ID3D12Resource>* buffer, uint64_t bytes, D3D12_RESOURCE_STATES state,
                  std::source_location where = std::source_location::current()) {
    if (*buffer && (*buffer)->GetDesc().Width >= bytes) return true;
    if (*buffer) releases_.push_back({.value = own_value_ + 1, .resource = std::move(*buffer)});
    *buffer = Buffer(Align(bytes, 1u << 16), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, state, where);
    return static_cast<bool>(*buffer);
  }

  // Named by the line that makes it (DRED's page fault report).
  ComPtr<ID3D12Resource> Buffer(uint64_t bytes, D3D12_HEAP_TYPE heap_type, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state,
                                std::source_location where = std::source_location::current()) const {
    const D3D12_HEAP_PROPERTIES heap = {.Type = heap_type};
    const D3D12_RESOURCE_DESC desc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Width = bytes,
        .Height = 1,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .SampleDesc = {.Count = 1},
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags = flags,
    };
    ComPtr<ID3D12Resource> buffer;
    if (SUCCEEDED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(buffer.GetAddressOf())))) {
      buffer->SetName(std::format(L"rt_d3d12.hpp:{}", where.line()).c_str());
      if (TestBuffers()) {
        buffers_made_.fetch_add(1, std::memory_order_relaxed);
      }
    }
    return buffer;
  }

  // RENODX_TW_TEST_BUFFERS (GPU harness): each report counts the buffers Buffer made since the last (warm: none a frame).
  static bool TestBuffers() {
    static const bool on = GetEnvironmentVariableW(L"RENODX_TW_TEST_BUFFERS", nullptr, 0) != 0;
    return on;
  }
  static inline std::atomic<uint32_t> buffers_made_ = 0;

  // RENODX_TW_TEST_ROOM=<count> (GPU harness): no memory for a room over that many placed instances or clusters.
  static uint32_t TestRoomLimit() {
    static const uint32_t limit = [] {
      std::array<wchar_t, 16> value = {};
      return (GetEnvironmentVariableW(L"RENODX_TW_TEST_ROOM", value.data(), static_cast<DWORD>(value.size())) != 0) ? static_cast<uint32_t>(_wtoi(value.data())) : UINT32_MAX;
    }();
    return limit;
  }

  // The placed instances' room: their descs (two in turn, the last one's carried instances copied into the next), the
  // counters and dedupe table (twice the descs), rt_place's dedupe scratch (written anew each request: not recorded), the
  // TLAS and its scratch. A bigger one is made on a worker and put in at a request's start.
  struct PlacedRoom {
    uint32_t capacity = 0;
    std::array<ComPtr<ID3D12Resource>, 2> instances;
    ComPtr<ID3D12Resource> state;
    ComPtr<ID3D12Resource> scratch;
    ComPtr<ID3D12Resource> tlas;
    ComPtr<ID3D12Resource> tlas_scratch;
  };

  // Its buffers (any thread). False: no memory for them.
  bool MakePlacedRoom(uint32_t capacity, PlacedRoom* made) const {
    if (capacity > TestRoomLimit()) return false;
    const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
        .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
        .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD,
        .NumDescs = capacity,
        .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
    };
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
    device_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
    made->capacity = capacity;
    for (auto& placed : made->instances) {
      placed = Buffer(uint64_t{capacity} * sizeof(D3D12_RAYTRACING_INSTANCE_DESC), D3D12_HEAP_TYPE_DEFAULT,
                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    made->state = Buffer(16 + (uint64_t{capacity} * 2 * 4), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    // rt_place's dedupe scratch: a table entry's owner and an item's slot (8 bytes) and their share of the groups' and
    // blocks' 16 bytes (one a 64 and a 4096 items) under 9 bytes an entry; a block's rounding and the blocks' counter in 32.
    made->scratch = Buffer(32 + (uint64_t{capacity} * 2 * 9), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    made->tlas = Buffer(info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    made->tlas_scratch = Buffer(info.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    return made->instances[0] && made->instances[1] && made->state && made->scratch && made->tlas && made->tlas_scratch;
  }

  // In place of the room before (released once the GPU is past this request).
  void UsePlacedRoom(PlacedRoom* made) {
    const std::array<std::pair<ComPtr<ID3D12Resource>*, ComPtr<ID3D12Resource>*>, 6> room = {{
        {std::addressof(placed_instances_[0]), std::addressof(made->instances[0])},
        {std::addressof(placed_instances_[1]), std::addressof(made->instances[1])},
        {std::addressof(placed_state_), std::addressof(made->state)},
        {std::addressof(placed_scratch_), std::addressof(made->scratch)},
        {std::addressof(tlas_), std::addressof(made->tlas)},
        {std::addressof(tlas_scratch_), std::addressof(made->tlas_scratch)},
    }};
    for (const auto& [now, next] : room) {
      if (*now) {
        releases_.push_back({.value = own_value_ + 1, .resource = std::move(*now)});
      }
      *now = std::move(*next);
    }
    placed_capacity_ = made->capacity;
  }

#if defined(RENODX_NVAPI_CLUSTERS)
  // A buffer for Mega Geometry's operations (written by them: unordered access).
  ComPtr<ID3D12Resource> ClusterBuffer(uint64_t bytes, D3D12_RESOURCE_STATES state, std::source_location where = std::source_location::current()) const {
    return Buffer(std::max<uint64_t>(bytes, 256), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, state, where);
  }

  bool ClusterRequirements(const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS& inputs,
                           NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO* info) const {
    const NVAPI_GET_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO_PARAMS params = {
        .version = NVAPI_GET_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO_PARAMS_VER,
        .pInput = &inputs,
        .pInfo = info,
    };
    return NvAPI_D3D12_GetRaytracingMultiIndirectClusterOperationRequirementsInfo(device_.Get(), &params) == NVAPI_OK;
  }

  // Room for `room` clusters a frame (a quarter of them built from triangles, the rest instantiated from templates): its
  // operations' inputs and its buffers: the CLASes (the instantiated first), the cluster BLASes (the units', the smoke's),
  // the scratch, the CLASes' addresses and arguments (32 and 64 bytes likewise). A bigger one is made on a worker while
  // the scene grows (its CreateCommittedResource calls, up to GBs, stalled the request's thread: hitches panning over the
  // map, ATN 2026-10-29) and put in at a request's start.
  struct ClusterRoom {
    uint32_t room = 0;
    uint32_t built = 0;  // of it
    NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS clas_inputs = {};
    NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS instantiate_inputs = {};
    NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS blas_inputs = {};
    uint64_t instantiated_bytes = 0;
    // The CLASes, the cluster BLASes (CLUSTER_BLASES), the scratch, the CLASes' addresses and arguments.
    std::array<uint64_t, 4 + CLUSTER_BLASES> bytes = {};
    std::array<ComPtr<ID3D12Resource>, 4 + CLUSTER_BLASES> buffers;
  };

  // Its inputs and buffers' bytes (the operations' requirements, on the request's thread) for `room` clusters a frame, of
  // them `built` from triangles. False: none.
  bool SizeClusterRoom(uint32_t room, uint32_t built, ClusterRoom* made) const {
    made->room = room;
    made->built = built;
    made->clas_inputs = clas_inputs_;
    made->clas_inputs.maxArgCount = built;
    made->instantiate_inputs = instantiate_inputs_;
    made->instantiate_inputs.maxArgCount = room - built;
    made->blas_inputs = cluster_blas_inputs_;
    made->blas_inputs.clasDesc = {.maxTotalClasCount = room, .maxClasCountPerArg = room};
    NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO clas_info = {};
    NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO blas_info = {};
    NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO templates_info = {};
    NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO instantiate_info = {};
    if (!ClusterRequirements(made->clas_inputs, &clas_info) || !ClusterRequirements(made->blas_inputs, &blas_info)
        || !ClusterRequirements(template_inputs_, &templates_info) || !ClusterRequirements(made->instantiate_inputs, &instantiate_info)) {
      return false;
    }
    made->instantiated_bytes = Align(instantiate_info.resultDataMaxSizeInBytes, NVAPI_D3D12_RAYTRACING_CLAS_BYTE_ALIGNMENT);
    made->bytes = {
        made->instantiated_bytes + clas_info.resultDataMaxSizeInBytes,
        std::max({clas_info.scratchDataSizeInBytes, blas_info.scratchDataSizeInBytes, templates_info.scratchDataSizeInBytes,
                  instantiate_info.scratchDataSizeInBytes}),
        uint64_t{room} * 8,
        (uint64_t{room - built} * 32) + (uint64_t{built} * 64),
    };
    std::fill(made->bytes.begin() + 4, made->bytes.end(), blas_info.resultDataMaxSizeInBytes);
    return true;
  }

  // Its buffers (any thread). False: no memory for them.
  bool MakeClusterRoom(ClusterRoom* made) const {
    if (made->room > TestRoomLimit()) return false;
    constexpr std::array<D3D12_RESOURCE_STATES, 4> STATES = {
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,                  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
    };
    for (size_t index = 0; index < made->buffers.size(); ++index) {
      made->buffers[index] = ClusterBuffer(made->bytes[index], (index < STATES.size()) ? STATES[index] : D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
      if (!made->buffers[index]) return false;
    }
    return true;
  }

  // In place of the room before (released once the GPU is past this request).
  void UseClusterRoom(ClusterRoom* made) {
    std::array<ComPtr<ID3D12Resource>*, 4 + CLUSTER_BLASES> room = {std::addressof(clas_), std::addressof(cluster_scratch_), std::addressof(clas_addresses_),
                                                                    std::addressof(cluster_args_)};
    for (size_t blas = 0; blas < CLUSTER_BLASES; ++blas) room[4 + blas] = std::addressof(cluster_blases_[blas]);
    for (size_t index = 0; index < room.size(); ++index) {
      if (*room[index]) {
        releases_.push_back({.value = own_value_ + 1, .resource = std::move(*room[index])});
      }
      *room[index] = std::move(made->buffers[index]);
    }
    clas_inputs_ = made->clas_inputs;
    instantiate_inputs_ = made->instantiate_inputs;
    cluster_blas_inputs_ = made->blas_inputs;
    instantiated_bytes_ = made->instantiated_bytes;
    cluster_room_ = made->room;
    built_limit_ = made->built;
    cluster_bytes_ = std::accumulate(made->bytes.begin(), made->bytes.end(), uint64_t{0});
  }
#endif

  // The kept positions' view without them (two slices, as the paths' shaders declare it).
  static constexpr D3D12_SHADER_RESOURCE_VIEW_DESC NO_KEPT_POSITIONS = {.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                                                                        .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY,
                                                                        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                                                        .Texture2DArray = {.MipLevels = 1, .ArraySize = 2}};
  // The ReGIR cells' view without them (a buffer, as RWBuffer<uint2> regir_cells declares it).
  static constexpr D3D12_UNORDERED_ACCESS_VIEW_DESC NO_REGIR_CELLS = {.Format = DXGI_FORMAT_R32G32_UINT, .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
                                                                      .Buffer = {.NumElements = 1}};
  // The paths' textures not shared (yet): null views, so their descriptors are always valid (the cache's too until
  // EnsureCache makes it).
  void WriteNullPathDescriptors() {
    const D3D12_SHADER_RESOURCE_VIEW_DESC texture_view = {.Format = DXGI_FORMAT_R8G8B8A8_UNORM, .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                                                          .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                                          .Texture2D = {.MipLevels = 1}};
    for (const auto descriptor : {PREVIOUS_COLOR, GUIDE_NORMALS, GUIDE_ALBEDO, GUIDE_ROUGHNESS}) {
      device_->CreateShaderResourceView(nullptr, &texture_view, Descriptor(descriptor));
    }
    device_->CreateShaderResourceView(nullptr, &NO_KEPT_POSITIONS, Descriptor(PREVIOUS_POSITIONS));
    const D3D12_UNORDERED_ACCESS_VIEW_DESC texture_written = {.Format = DXGI_FORMAT_R8G8B8A8_UNORM, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
    for (UINT set = 0; set < 2; ++set) {
      for (const UINT descriptor : {REUSE_SAMPLES, REUSE_DATA, REFLECT_SAMPLES, REFLECT_DATA, OWN_SKY, LIGHT_RATIO}) {
        device_->CreateUnorderedAccessView(nullptr, nullptr, &texture_written, Descriptor(descriptor + set));
      }
    }
    for (const UINT descriptor : std::initializer_list<UINT>{SPECULAR_ACCUMULATED, SPECULAR_HISTORY, SPECULAR_META, SPECULAR_META_HISTORY,
                                                             REFLECTION, SPECULAR_OCCLUSION, LIGHT_MASK, FOG, AMBIENT_HISTORY,
                                                             AMBIENT_HISTORY + 1, AMBIENT_POSITIONS, HIT_DISTANCE, LIGHT_HISTORY,
                                                             LIGHT_HISTORY + 1, FOG_LIGHTS, TINT, SURFACE_ORIGIN, SURFACE_NORMAL}) {
      device_->CreateUnorderedAccessView(nullptr, nullptr, &texture_written, Descriptor(descriptor));
    }
    // The reference build's slots (its run writes u60-u72 there).
#if defined(RENODX_REFERENCE)
    constexpr UINT unused_from = RTXDI_GI_RESERVOIRS;
#else
    constexpr UINT unused_from = REF_CANDIDATE_BATCH;
#endif
    for (UINT descriptor = unused_from; descriptor < DESCRIPTOR_COUNT; ++descriptor) {
      device_->CreateUnorderedAccessView(nullptr, nullptr, &texture_written, Descriptor(descriptor));
    }
    device_->CreateUnorderedAccessView(nullptr, nullptr, &NO_REGIR_CELLS, Descriptor(REGIR_CELLS));
    if (cache_keys_) return;
    const D3D12_UNORDERED_ACCESS_VIEW_DESC buffer_written = {.Format = DXGI_FORMAT_R32_UINT, .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
                                                             .Buffer = {.NumElements = 1}};
    for (const auto descriptor : {CACHE_KEYS, CACHE_RADIANCE, CACHE_SUMS, CACHE_ALBEDO, LIGHT_FIT, LIGHT_KEPT}) {
      device_->CreateUnorderedAccessView(nullptr, nullptr, &buffer_written, Descriptor(descriptor));
    }
  }

#if defined(RENODX_REFERENCE)
  // The reference build's run (RENODX_TW_REFERENCE; tests only; rt_reference.hlsli; tools/tw/round_notes/plan_1117/
  // reference_suite.md 2.1-2.2): the effect's requests counted; after `settle` of them a window of `frames` in which its
  // candidate output is accumulated by 16-frame batches (B1) with the census of its outputs and each trace's scene digest;
  // at the window's end every input the reference reads copied into its own resources (B2), bound alone in a second
  // descriptor table; then `groups` x (`total_samples` / `groups`) samples a texel in slices of `slice_samples`, one a
  // request (a slice on a frame whose digest is not the freeze frame's is dropped and counted); then every sum, the frozen
  // inputs (their hashes) and the state into `folder`: meta.json, census.json, a .bin a texture (rows packed).
  // RENODX_TW_REF_SETTLE (S, 150), _FRAMES (K, 384, a multiple of 16), _GROUPS (G, 8), _SEED, _VARIANT (full, same_start,
  // furnace, lout, contact, same_route, contact_sky: rt_reference.hlsli REF_*), _SCALE (the synthetic control: the reference
  // x it), _BREAK (the code-break control: the reference wrong in one known way, REF_BREAKS; a name not there: no run),
  // _INSIDE (count, the default: hits inside a solid counted; dark: they end their paths too, REF_INSIDE_DARK; else no run),
  // _FURNACE (<albedo>,<light>), _DUMP (the folder).
  enum class ReferenceEffect : uint32_t { PATHS, REFLECT };
  enum class ReferencePhase : uint32_t { OFF, SETTLE, WINDOW, SLICES, DUMPING, DONE };
  enum class ReferenceKind : uint32_t { TRACE, BOUNCE, REFLECTIONS };
  // rt_reference.hlsli's words.
  static constexpr uint32_t REF_STATE_WORDS = 256;
  static constexpr uint32_t REF_STATE_GROUP_SLICES = 144;
  static constexpr uint32_t REF_OP_CLEAR = 0;
  static constexpr uint32_t REF_OP_DIGEST_BEGIN = 1;
  static constexpr uint32_t REF_OP_DIGEST_END = 2;
  static constexpr uint32_t REF_OP_DIGEST_FREEZE = 3;
  static constexpr uint32_t REF_OP_GROUP_DONE = 4;
  static constexpr uint32_t REF_BATCH = 16;
  static constexpr uint32_t REF_SCALED = 256;
  static constexpr uint32_t REF_DIRECT = 512;
  static constexpr uint32_t REF_BREAK_SHIFT = 10;  // rt_reference.hlsli REF_BREAK_*: their index in REF_BREAKS ("": none)
  static constexpr std::array<std::string_view, 6> REF_BREAKS = {"", "nocal", "range_half", "cone2", "onelight", "no_g2g1"};
  static constexpr uint32_t REF_INSIDE_DARK = 1u << 14;  // rt_reference.hlsli's: REF_INSIDE_MODES's "dark"
  static constexpr std::array<std::string_view, 3> REF_INSIDE_MODES = {"", "count", "dark"};
  static constexpr uint32_t REF_SOURCE_BOUNCE = 0;
  static constexpr uint32_t REF_SOURCE_SPECULAR_OCCLUSION = 2;
  static constexpr uint64_t REF_GROUP_PICTURES_MAX = uint64_t{1920} * 1080;  // grids up to this keep each group's mean (G3)
  static constexpr std::array<std::string_view, 7> REF_VARIANTS = {"full", "same_start", "furnace", "lout", "contact", "same_route", "contact_sky"};
  // REF_CENSUS's outputs (rt_reference.hlsli REF_OUT_*).
  static constexpr std::array<std::string_view, 12> REF_OUTPUTS = {"bounce", "reflection", "specular", "own_sky_0", "own_sky_1", "reuse_0", "reuse_1",
                                                                  "fog_lights", "ref_mean", "candidate_mean", "ref_m2", "candidate_m2"};
  enum class ReferenceView : uint32_t { TEXTURE, ARRAY, TEXTURE_UAV, BUFFER_UAV };
  // A frozen input: its source and the state its request holds it in (one subresource, or UINT32_MAX: all), its view in the
  // frozen table, whether its file is written (else only hashed), its copy.
  struct ReferenceFrozen {
    const char* name = "";
    ID3D12Resource* source = nullptr;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    UINT subresource = UINT32_MAX;
    UINT descriptor = 0;
    ReferenceView view = ReferenceView::TEXTURE;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    UINT elements = 0;  // a buffer's
    bool written = false;
    ComPtr<ID3D12Resource> copy;
  };
  // A resource read back at the end: a footprint a subresource in the readback buffer (a buffer's: its bytes as one row).
  struct ReferenceRead {
    std::string name;
    ID3D12Resource* resource = nullptr;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    bool written = false;
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints;
    std::vector<UINT> rows;
    std::vector<UINT64> row_bytes;
  };
  struct Reference {
    ReferenceEffect effect = ReferenceEffect::PATHS;
    uint32_t slice_samples = 64;
    uint32_t total_samples = 4096;
    uint32_t settle = 150;
    uint32_t frames = 384;
    uint32_t groups = 8;
    uint32_t seed = 0;
    uint32_t variant = 0;
    float scale = 1.f;
    bool direct = false;  // RENODX_TW_REF_DIRECT: the texel's own direct light of the bounce's lights too (REF_DIRECT)
    std::array<char, 32> code_break_asked = {};  // RENODX_TW_REF_BREAK
    uint32_t code_break = 0;                     // its index in REF_BREAKS (REF_BREAKS.size(): unknown)
    std::array<char, 16> inside_asked = {};      // RENODX_TW_REF_INSIDE
    uint32_t inside_mode = 0;                    // its index in REF_INSIDE_MODES (REF_INSIDE_MODES.size(): unknown)
    std::array<float, 2> furnace = {0.5f, 1.f};  // albedo, light
    std::filesystem::path folder;
    std::string note;  // its line for the log
    ReferencePhase phase = ReferencePhase::OFF;
    uint32_t effect_frames = 0;
    uint32_t freeze_frame = 0;
    std::array<uint32_t, 2> grid = {};
    bool pictures = false;  // each group's mean kept
    uint64_t dump_value = 0;
    std::array<uint32_t, 12> trace_settings = {};   // the last trace's b0
    std::array<uint32_t, 12> frozen_settings = {};  // at the freeze
    std::array<uint32_t, 2> mesh_hash = {};          // the digest's CPU half (the meshes' keys, sizes, kinds, micromaps, builds)
    std::array<uint32_t, 2> frozen_mesh_hash = {};
    ComPtr<ID3D12PipelineState> accumulate;
    ComPtr<ID3D12PipelineState> census;
    ComPtr<ID3D12PipelineState> digest;
    ComPtr<ID3D12PipelineState> state;
    ComPtr<ID3D12PipelineState> group;
    ComPtr<ID3D12PipelineState> paths;
    ComPtr<ID3D12PipelineState> reflect;
    std::array<ComPtr<ID3D12Resource>, REF_STATE - REF_CANDIDATE_BATCH + 1> outputs;
    std::vector<ReferenceFrozen> frozen;
    ComPtr<ID3D12Resource> constants;       // the frozen b3 (at 0) and b4 (at 256), an upload buffer
    std::vector<uint8_t> frozen_constants;  // b0, b3 and b4's bytes at the freeze (the frame's number left out)
    ComPtr<ID3D12Resource> readback;
    std::vector<ReferenceRead> reads;
  };
  // A request's part (ReferenceRequest): its kind and frame, the sizes, what its list holds bound (put back after the
  // reference's dispatches), a trace's TLAS inputs, which shared textures it holds in shader states.
  struct ReferenceCall {
    ReferenceKind kind = ReferenceKind::TRACE;
    uint32_t frame = 0;
    std::array<uint32_t, 2> render = {};
    std::array<uint32_t, 2> bounce = {};
    const std::array<uint32_t, 12>* settings = nullptr;  // b0
    const std::array<uint32_t, 20>* bounce_constants = nullptr;  // b2 (a paths request's)
    D3D12_GPU_VIRTUAL_ADDRESS paths = 0;  // b3
    D3D12_GPU_VIRTUAL_ADDRESS lights = 0;  // b4
    D3D12_GPU_VIRTUAL_ADDRESS capture_table = 0;  // t8
    const PathConstants* path_constants = nullptr;
    const uint8_t* light_data = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS instance_descs = 0;  // a trace's
    uint32_t instances = 0;
    bool guides = false;  // the guides in NON_PIXEL_SHADER_RESOURCE
  };
  Reference reference_;

  // The reference's part of a request (Trace's Record and Paths, after their passes, on their list's bindings).
  void ReferenceRequest(ID3D12GraphicsCommandList4* list, const ReferenceCall& call) {
    Reference& run = reference_;
    if (run.phase == ReferencePhase::OFF || run.phase == ReferencePhase::DONE) return;
    // A break the shaders do not know would run unbroken and read as a control that failed to fail.
    if (run.code_break >= REF_BREAKS.size() || run.inside_mode >= REF_INSIDE_MODES.size()) {
      run.phase = ReferencePhase::OFF;
      slow_report_ += std::format("\n[TW reference] RENODX_TW_REF_BREAK '{}' (nocal, range_half, cone2, onelight, no_g2g1) or RENODX_TW_REF_INSIDE '{}' "
                                  "(count, dark) unknown or too long: off",
                                  run.code_break_asked.data(), run.inside_asked.data());
      return;
    }
    const D3D12_RESOURCE_BARRIER uav_barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV};
    // FNV-1a, 64 bits.
    const auto hash_bytes = [](std::span<const uint8_t> bytes, uint64_t hash = 14695981039346656037ull) {
      for (const uint8_t byte : bytes) hash = (hash ^ byte) * 1099511628211ull;
      return hash;
    };
    const auto set_words = [&](const std::array<uint32_t, 8>& words) { list->SetComputeRoot32BitConstants(ROOT_DISPATCH, 8, words.data(), 0); };
    const auto state_op = [&](uint32_t op, uint32_t a, uint32_t b) {
      set_words({op, a, b, 0u, 0u, 0u, 0u, 0u});
      list->SetPipelineState(run.state.Get());
      list->Dispatch(1, 1, 1);
      list->ResourceBarrier(1, &uav_barrier);
    };
    // A pass over a grid of words[6] x words[7].
    const auto grid_pass = [&](ID3D12PipelineState* pass, const std::array<uint32_t, 8>& words) {
      set_words(words);
      list->SetPipelineState(pass);
      list->Dispatch((words[6] + 7) / 8, (words[7] + 7) / 8, 1);
      list->ResourceBarrier(1, &uav_barrier);
    };
    if (run.phase == ReferencePhase::DUMPING) {
      // The last slice's request is done: the files from its readback.
      run.phase = ReferencePhase::DONE;
      void* mapped = nullptr;
      std::error_code made;
      std::filesystem::create_directories(run.folder, made);
      if (!WaitFor(run.dump_value) || FAILED(run.readback->Map(0, nullptr, &mapped))) {
        slow_report_ += "\n[TW reference] its readback failed";
        return;
      }
      std::string files;
      std::string hashes;
      std::array<uint32_t, REF_STATE_WORDS> state = {};
      for (const ReferenceRead& read : run.reads) {
        std::vector<uint8_t> packed;
        for (size_t sub = 0; sub < read.footprints.size(); ++sub) {
          const auto* bytes = static_cast<const uint8_t*>(mapped) + read.footprints[sub].Offset;
          for (UINT row = 0; row < read.rows[sub]; ++row) {
            const uint8_t* line = bytes + (uint64_t{row} * read.footprints[sub].Footprint.RowPitch);
            packed.insert(packed.end(), line, line + read.row_bytes[sub]);
          }
        }
        if (read.name == "state") std::memcpy(state.data(), packed.data(), std::min(packed.size(), sizeof(state)));
        const uint64_t hash = hash_bytes(packed);
        const D3D12_RESOURCE_DESC desc = read.resource->GetDesc();
        files += std::format("{}\n    \"{}\": {{\"format\": {}, \"width\": {}, \"height\": {}, \"slices\": {}, \"row_bytes\": {}, \"hash\": \"{:016x}\"}}",
                             files.empty() ? "" : ",", read.name, static_cast<uint32_t>(desc.Format), desc.Width, desc.Height,
                             (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) ? 1u : desc.DepthOrArraySize, read.row_bytes[0], hash);
        if (read.name.starts_with("frozen_")) hashes += std::format("{}\"{}\": \"{:016x}\"", hashes.empty() ? "" : ", ", read.name.substr(7), hash);
        if (!read.written) continue;
        std::ofstream file(run.folder / (read.name + ".bin"), std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(packed.data()), static_cast<std::streamsize>(packed.size()));
      }
      const D3D12_RANGE written = {};
      run.readback->Unmap(0, &written);
      std::string census;
      for (size_t output = 0; output < REF_OUTPUTS.size(); ++output) {
        const uint32_t* counts = state.data() + (output * 4);
        const uint32_t first = state[64 + output];
        census += std::format("{}\n  \"{}\": {{\"nan\": {}, \"inf\": {}, \"negative\": {}, \"over_fp16\": {}, \"first\": [{}, {}]}}", census.empty() ? "" : ",",
                              REF_OUTPUTS[output], counts[0], counts[1], counts[2], counts[3], (first != 0) ? static_cast<int>((first - 1) & 0xFFFFu) : -1,
                              (first != 0) ? static_cast<int>((first - 1) >> 16) : -1);
      }
      std::ofstream(run.folder / "census.json", std::ios::trunc) << "{" << census << "\n}\n";
      std::string group_slices;
      for (uint32_t group = 0; group < run.groups; ++group) group_slices += std::format("{}{}", group == 0 ? "" : ", ", state[REF_STATE_GROUP_SLICES + group]);
      std::string settings;
      for (const uint32_t word : run.frozen_settings) settings += std::format("{}{}", settings.empty() ? "" : ", ", word);
      PathConstants paths = {};
      std::memcpy(&paths, run.frozen_constants.data() + sizeof(run.frozen_settings), sizeof(paths));
      uint32_t light_count = 0;
      std::memcpy(&light_count, run.frozen_constants.data() + sizeof(run.frozen_settings) + sizeof(PathConstants) + LIGHT_COUNT_OFFSET, sizeof(light_count));
      std::ofstream(run.folder / "meta.json", std::ios::trunc) << std::format(
          "{{\n  \"suite\": \"ref1\",\n  \"effect\": \"{}\",\n  \"variant\": \"{}\",\n  \"scale\": {},\n  \"direct\": {},\n  \"break\": \"{}\",\n  \"inside\": \"{}\",\n  \"furnace\": [{}, {}],\n"
          "  \"samples\": {{\"slice\": {}, \"total\": {}, \"groups\": {}, \"per_group\": {}}},\n  \"settle\": {},\n  \"frames\": {},\n  \"batch\": {},\n"
          "  \"seed\": {},\n  \"grid\": [{}, {}],\n  \"freeze_frame\": {},\n  \"settings\": [{}],\n"
          "  \"paths\": {{\"flags\": {}, \"bounces\": {}, \"samples\": {}, \"bounce_range\": {}, \"reflection_range\": {}, \"sky_range\": {}, "
          "\"bounce_size\": [{}, {}, {}]}},\n  \"lights\": {},\n"
          "  \"digest\": {{\"frozen\": \"{:08x}{:08x}{:08x}{:08x}\", \"cpu\": \"{:08x}{:08x}\", \"changes_in_window\": {}, \"dropped_slices\": {}, "
          "\"groups_done\": {}, \"group_slices\": [{}]}},\n  \"frozen\": {{{}, \"constants\": \"{:016x}\"}},\n  \"files\": {{{}\n  }}\n}}\n",
          (run.effect == ReferenceEffect::REFLECT) ? "reflect" : "paths", REF_VARIANTS[run.variant], run.scale, run.direct, REF_BREAKS[run.code_break], (REF_INSIDE_MODES[run.inside_mode] == "dark") ? "dark" : "count", run.furnace[0], run.furnace[1],
          run.slice_samples, run.total_samples, run.groups, run.total_samples / run.groups, run.settle, run.frames, REF_BATCH, run.seed, run.grid[0],
          run.grid[1], run.freeze_frame, settings, paths.flags, paths.bounces, paths.samples, paths.bounce_range, paths.reflection_range, paths.sky_range,
          paths.bounce_width, paths.bounce_height, paths.bounce_scale, light_count, state[132], state[133], state[134], state[135],
          run.frozen_mesh_hash[0], run.frozen_mesh_hash[1], state[141], state[143], state[208], group_slices, hashes, hash_bytes(run.frozen_constants),
          files);
      slow_report_ += std::format("\n[TW reference] done: {}", run.folder.string());
      run.readback.Reset();
      run.frozen.clear();
      run.reads.clear();
      run.outputs = {};
      run.constants.Reset();
      return;
    }
    if (call.kind == ReferenceKind::TRACE) {
      run.trace_settings = *call.settings;
      if (run.phase != ReferencePhase::WINDOW && run.phase != ReferencePhase::SLICES) return;
      // The scene digest (B2): the TLAS's instance descs and the records (REF_DIGEST, on the GPU), the meshes on the CPU
      // (order-free sums: unordered_map's order is no scene's).
      uint32_t sum = 0;
      uint32_t mixed = 0;
      for (const auto& [key, mesh] : meshes_) {
        const std::array<uint64_t, 4> fields = {key,
                                                (uint64_t{mesh.triangles} << 32) | (mesh.foliage ? 1u : 0u) | (mesh.grass ? 2u : 0u) | (mesh.solid_cut_out ? 4u : 0u)
                                                    | (mesh.placed ? 8u : 0u) | (mesh.micromaps ? 16u : 0u),
                                                mesh.build, mesh.transforms.size()};
        const uint64_t hash = hash_bytes({reinterpret_cast<const uint8_t*>(mesh.transforms.data()), mesh.transforms.size() * sizeof(mesh.transforms[0])},
                                         hash_bytes({reinterpret_cast<const uint8_t*>(fields.data()), sizeof(fields)}));
        sum += static_cast<uint32_t>(hash);
        mixed ^= static_cast<uint32_t>(hash >> 32);
      }
      run.mesh_hash = {sum, mixed};
      list->ResourceBarrier(1, &uav_barrier);
      state_op(REF_OP_DIGEST_BEGIN, 0, 0);
      list->SetPipelineState(run.digest.Get());
      // Instance descs (64 bytes) one a thread; records four a thread.
      const std::array<std::pair<D3D12_GPU_VIRTUAL_ADDRESS, uint32_t>, 2> lanes = {{{call.instance_descs, call.instances}, {records_->GetGPUVirtualAddress(), (CLUSTER_RECORDS + 3) / 4}}};
      for (uint32_t lane = 0; lane < lanes.size(); ++lane) {
        const auto& [address, elements] = lanes[lane];
        if (elements == 0) continue;
        list->SetComputeRootShaderResourceView(ROOT_CAPTURE_TABLE, address);
        set_words({lane, elements, 0u, 0u, 0u, 0u, 0u, 0u});
        list->Dispatch((elements + 63) / 64, 1, 1);
      }
      list->ResourceBarrier(1, &uav_barrier);
      list->SetComputeRootShaderResourceView(ROOT_CAPTURE_TABLE, call.capture_table);
      state_op(REF_OP_DIGEST_END, sum, mixed);
      return;
    }
    if (call.kind != ((run.effect == ReferenceEffect::REFLECT) ? ReferenceKind::REFLECTIONS : ReferenceKind::BOUNCE)) return;
    // After the request's passes (their outputs written).
    list->ResourceBarrier(1, &uav_barrier);
    const uint32_t seen = run.effect_frames++;
    if (seen < run.settle) return;
    const uint32_t index = seen - run.settle;
    const std::array<uint32_t, 2> grid = (run.effect == ReferenceEffect::PATHS) ? call.bounce : call.render;
    const D3D12_GPU_DESCRIPTOR_HANDLE live_table = descriptors_->GetGPUDescriptorHandleForHeapStart();
    const D3D12_GPU_DESCRIPTOR_HANDLE frozen_table = {.ptr = live_table.ptr + (uint64_t{DESCRIPTOR_COUNT} * descriptor_size_)};
    if (index == 0) {
      // The run's own resources and their views in both tables (the frozen table's other views come at the freeze).
      run.grid = grid;
      run.pictures = uint64_t{grid[0]} * grid[1] <= REF_GROUP_PICTURES_MAX;
      bool made = true;
      for (UINT output = REF_CANDIDATE_BATCH; output <= REF_STATE; ++output) {
        ComPtr<ID3D12Resource>& resource = run.outputs[output - REF_CANDIDATE_BATCH];
        if (output == REF_STATE) {
          resource = Buffer(uint64_t{REF_STATE_WORDS} * 4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
          const D3D12_UNORDERED_ACCESS_VIEW_DESC view = {.Format = DXGI_FORMAT_R32_UINT, .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
                                                         .Buffer = {.NumElements = REF_STATE_WORDS}};
          for (const UINT table : {0u, UINT{DESCRIPTOR_COUNT}}) device_->CreateUnorderedAccessView(resource.Get(), nullptr, &view, Descriptor(table + output));
        } else if (output == REF_GROUPS) {
          const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
          const D3D12_RESOURCE_DESC desc = {.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D, .Width = run.pictures ? grid[0] : 1u,
                                            .Height = run.pictures ? grid[1] : 1u, .DepthOrArraySize = static_cast<UINT16>(run.pictures ? run.groups : 1u),
                                            .MipLevels = 1, .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .SampleDesc = {.Count = 1},
                                            .Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS};
          if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                      IID_PPV_ARGS(resource.GetAddressOf())))) {
            resource.Reset();
          }
          const D3D12_UNORDERED_ACCESS_VIEW_DESC view = {.Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY,
                                                         .Texture2DArray = {.ArraySize = desc.DepthOrArraySize}};
          for (const UINT table : {0u, UINT{DESCRIPTOR_COUNT}}) device_->CreateUnorderedAccessView(resource.Get(), nullptr, &view, Descriptor(table + output));
        } else {
          const DXGI_FORMAT format = (output == REF_AUX) ? DXGI_FORMAT_R32G32B32A32_UINT : DXGI_FORMAT_R32G32B32A32_FLOAT;
          if (!Texture(format, &resource)) resource.Reset();
          for (const UINT table : {0u, UINT{DESCRIPTOR_COUNT}}) Written(resource.Get(), format, table + output);
        }
        made = made && resource;
      }
      if (!made) {
        run.phase = ReferencePhase::OFF;
        run.outputs = {};
        slow_report_ += "\n[TW reference] no memory for its sums: off";
        return;
      }
      state_op(REF_OP_CLEAR, 0, REF_STATE_WORDS);
      run.phase = ReferencePhase::WINDOW;
    }
    if (index < run.frames) {
      // The candidate's output into its batch sums; the census of the request's outputs (B1, I10).
      const bool paths_effect = run.effect == ReferenceEffect::PATHS;
      grid_pass(run.accumulate.Get(), {paths_effect ? REF_SOURCE_BOUNCE : REF_SOURCE_SPECULAR_OCCLUSION, index % REF_BATCH, index / REF_BATCH, 0u, 0u, 0u,
                                       grid[0], grid[1]});
      // rt_reference.hlsli REF_OUT_*: a Light Bounce's bounce, own sky and reservoirs at its texels; reflections' at the pixels.
      const std::array<uint32_t, 5> outputs = paths_effect ? std::array<uint32_t, 5>{0u, 3u, 4u, 5u, 6u} : std::array<uint32_t, 5>{1u, 2u, 1u, 2u, 2u};
      for (const uint32_t output : outputs) grid_pass(run.census.Get(), {output, 0u, 0u, 0u, 0u, 0u, grid[0], grid[1]});
      if (index + 1 < run.frames) return;
      // The window's end: every input the reference reads copied (B2), the copies' views in the frozen table, the constants.
      run.freeze_frame = call.frame;
      run.frozen.clear();
      const auto add = [&](ReferenceFrozen frozen) {
        if (frozen.source != nullptr) run.frozen.push_back(std::move(frozen));
      };
      constexpr auto SHADER_READ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
      constexpr auto WRITTEN = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      const D3D12_RESOURCE_STATES guide_state = call.guides ? SHADER_READ : D3D12_RESOURCE_STATE_COMMON;
      add({.name = "positions", .source = positions_.Get(), .state = SHADER_READ, .descriptor = POSITIONS, .format = DXGI_FORMAT_R32G32B32A32_FLOAT, .written = true});
      add({.name = "sun", .source = sun_.Get(), .state = SHADER_READ, .descriptor = SUN, .format = DXGI_FORMAT_R32G32B32A32_FLOAT, .written = true});
      add({.name = "light", .source = light_.Get(), .state = SHADER_READ, .descriptor = LIGHT, .format = DXGI_FORMAT_R11G11B10_FLOAT, .written = true});
      add({.name = "previous_color", .source = previous_color_.Get(), .state = SHADER_READ, .descriptor = PREVIOUS_COLOR, .format = DXGI_FORMAT_R16G16B16A16_FLOAT});
      add({.name = "previous_positions", .source = kept_positions_.Get(), .state = SHADER_READ, .subresource = previous_positions_slice_,
           .descriptor = PREVIOUS_POSITIONS, .view = ReferenceView::ARRAY, .format = DXGI_FORMAT_R32G32B32A32_FLOAT});
      add({.name = "guide_normals", .source = guide_normals_.Get(), .state = guide_state, .descriptor = GUIDE_NORMALS, .format = DXGI_FORMAT_R16G16B16A16_FLOAT,
           .written = true});
      add({.name = "guide_albedo", .source = guide_albedo_.Get(), .state = guide_state, .descriptor = GUIDE_ALBEDO, .format = DXGI_FORMAT_R8G8B8A8_UNORM, .written = true});
      add({.name = "guide_roughness", .source = guide_roughness_.Get(), .state = guide_state, .descriptor = GUIDE_ROUGHNESS, .format = DXGI_FORMAT_R8_UNORM,
           .written = true});
      add({.name = "visibility", .source = visibility_.Get(), .state = WRITTEN, .descriptor = VISIBILITY, .view = ReferenceView::TEXTURE_UAV,
           .format = DXGI_FORMAT_R8_UNORM, .written = true});
      add({.name = "cache_keys", .source = cache_keys_.Get(), .state = WRITTEN, .descriptor = CACHE_KEYS, .view = ReferenceView::BUFFER_UAV,
           .format = DXGI_FORMAT_R32_UINT, .elements = CACHE_ENTRIES});
      add({.name = "cache_radiance", .source = cache_radiance_.Get(), .state = WRITTEN, .descriptor = CACHE_RADIANCE, .view = ReferenceView::BUFFER_UAV,
           .format = DXGI_FORMAT_R32G32B32A32_FLOAT, .elements = CACHE_ENTRIES});
      add({.name = "cache_albedo", .source = cache_albedo_.Get(), .state = WRITTEN, .descriptor = CACHE_ALBEDO, .view = ReferenceView::BUFFER_UAV,
           .format = DXGI_FORMAT_R32G32_UINT, .elements = CACHE_ENTRIES});
      add({.name = "light_fit", .source = light_fit_.Get(), .state = WRITTEN, .descriptor = LIGHT_FIT, .view = ReferenceView::BUFFER_UAV,
           .format = DXGI_FORMAT_R32_UINT, .elements = 16});
      add({.name = "light_kept", .source = light_kept_.Get(), .state = WRITTEN, .descriptor = LIGHT_KEPT, .view = ReferenceView::BUFFER_UAV,
           .format = DXGI_FORMAT_R32G32B32A32_FLOAT, .elements = LIGHT_KEPT_TEXELS, .written = true});
      for (UINT set = 0; set < 2; ++set) {
        add({.name = (set == 0) ? "own_sky_0" : "own_sky_1", .source = own_sky_[set].Get(), .state = WRITTEN, .descriptor = OWN_SKY + set,
             .view = ReferenceView::TEXTURE_UAV, .format = DXGI_FORMAT_R16G16B16A16_FLOAT});
      }
#if defined(RENODX_REGIR)
      // Torch Light in Path-Traced GI's (PT DI step 3: T1, T2).
      add({.name = "torch_irradiance", .source = torch_irradiance_.Get(), .state = WRITTEN, .descriptor = TORCH_IRRADIANCE, .view = ReferenceView::TEXTURE_UAV,
           .format = DXGI_FORMAT_R16G16B16A16_FLOAT});
      if (regir_cells_) {
        add({.name = "regir_cells", .source = regir_cells_.Get(), .state = WRITTEN, .descriptor = REGIR_CELLS, .view = ReferenceView::BUFFER_UAV,
             .format = DXGI_FORMAT_R32G32_UINT, .elements = static_cast<UINT>(regir_cells_->GetDesc().Width / 8)});
      }
#endif
      // Every slot of the frozen table null of its kind first; the scene's own (it cannot be copied: the digest guards it).
      const D3D12_SHADER_RESOURCE_VIEW_DESC null_texture = {.Format = DXGI_FORMAT_R8G8B8A8_UNORM, .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                                                            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                                            .Texture2D = {.MipLevels = 1}};
      const D3D12_UNORDERED_ACCESS_VIEW_DESC null_written = {.Format = DXGI_FORMAT_R8G8B8A8_UNORM, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
      for (UINT slot = 0; slot < DESCRIPTOR_COUNT; ++slot) {
        if (slot >= REF_CANDIDATE_BATCH && slot <= REF_STATE) continue;  // the reference's own outputs (both tables, below)
        const bool written_slot = (slot >= SRV_COUNT && slot < LIGHT) || slot == BOUNCE || slot >= CACHE_KEYS;
        if (written_slot) {
          device_->CreateUnorderedAccessView(nullptr, nullptr, &null_written, Descriptor(DESCRIPTOR_COUNT + slot));
        } else {
          device_->CreateShaderResourceView(nullptr, &null_texture, Descriptor(DESCRIPTOR_COUNT + slot));
        }
      }
      const auto buffer_view = [](DXGI_FORMAT format, UINT elements) {
        return D3D12_SHADER_RESOURCE_VIEW_DESC{.Format = format, .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                                               .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING, .Buffer = {.NumElements = elements}};
      };
      const auto records_view = buffer_view(DXGI_FORMAT_R32G32B32A32_UINT, RECORD_COUNT);
      const auto uvs_view = buffer_view(DXGI_FORMAT_R32_UINT, uv_pool_);
      const auto normals_view = buffer_view(DXGI_FORMAT_R32_UINT, NORMAL_POOL);
      const auto vertices_view = buffer_view(DXGI_FORMAT_R32G32B32A32_FLOAT, TRANSFER_VERTICES);
      const D3D12_SHADER_RESOURCE_VIEW_DESC atlas_view = {.Format = DXGI_FORMAT_R8_UNORM, .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY,
                                                          .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                                          .Texture2DArray = {.MipLevels = 1, .ArraySize = dlaa_bridge::RT_ATLAS_SLICES}};
      device_->CreateShaderResourceView(records_.Get(), &records_view, Descriptor(DESCRIPTOR_COUNT + RECORDS));
      device_->CreateShaderResourceView(uvs_.Get(), &uvs_view, Descriptor(DESCRIPTOR_COUNT + UVS));
      device_->CreateShaderResourceView(normals_.Get(), &normals_view, Descriptor(DESCRIPTOR_COUNT + NORMALS));
      device_->CreateShaderResourceView(vertex_buffer_.Get(), &vertices_view, Descriptor(DESCRIPTOR_COUNT + VERTICES));
      device_->CreateShaderResourceView(atlas_.Get(), &atlas_view, Descriptor(DESCRIPTOR_COUNT + ATLAS));
      std::vector<D3D12_RESOURCE_BARRIER> to_copy;
      for (ReferenceFrozen& frozen : run.frozen) {
        D3D12_RESOURCE_DESC desc = frozen.source->GetDesc();
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        desc.Alignment = 0;
        const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
        if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                    IID_PPV_ARGS(frozen.copy.GetAddressOf())))) {
          run.phase = ReferencePhase::OFF;
          slow_report_ += std::format("\n[TW reference] no memory for its frozen {}: off", frozen.name);
          return;
        }
        D3D12_RESOURCE_BARRIER barrier = Transition(frozen.source, frozen.state, D3D12_RESOURCE_STATE_COPY_SOURCE);
        if (frozen.subresource != UINT32_MAX) barrier.Transition.Subresource = frozen.subresource;
        if (frozen.state != D3D12_RESOURCE_STATE_COPY_SOURCE) to_copy.push_back(barrier);
      }
      list->ResourceBarrier(static_cast<UINT>(to_copy.size()), to_copy.data());
      std::vector<D3D12_RESOURCE_BARRIER> after;
      for (const ReferenceFrozen& frozen : run.frozen) {
        if (frozen.subresource == UINT32_MAX) {
          list->CopyResource(frozen.copy.Get(), frozen.source);
        } else {
          const D3D12_TEXTURE_COPY_LOCATION to = {.pResource = frozen.copy.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, .SubresourceIndex = frozen.subresource};
          const D3D12_TEXTURE_COPY_LOCATION from = {.pResource = frozen.source, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, .SubresourceIndex = frozen.subresource};
          list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        }
        const bool uav = frozen.view == ReferenceView::TEXTURE_UAV || frozen.view == ReferenceView::BUFFER_UAV;
        after.push_back(Transition(frozen.copy.Get(), D3D12_RESOURCE_STATE_COPY_DEST, uav ? WRITTEN : SHADER_READ));
        const D3D12_CPU_DESCRIPTOR_HANDLE slot = Descriptor(DESCRIPTOR_COUNT + frozen.descriptor);
        switch (frozen.view) {
          case ReferenceView::TEXTURE: {
            const D3D12_SHADER_RESOURCE_VIEW_DESC view = {.Format = frozen.format, .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                                                          .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING, .Texture2D = {.MipLevels = 1}};
            device_->CreateShaderResourceView(frozen.copy.Get(), &view, slot);
            break;
          }
          case ReferenceView::ARRAY: {
            const D3D12_SHADER_RESOURCE_VIEW_DESC view = {.Format = frozen.format, .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY,
                                                          .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                                          .Texture2DArray = {.MipLevels = 1, .ArraySize = frozen.copy->GetDesc().DepthOrArraySize}};
            device_->CreateShaderResourceView(frozen.copy.Get(), &view, slot);
            break;
          }
          case ReferenceView::TEXTURE_UAV:
            Written(frozen.copy.Get(), frozen.format, DESCRIPTOR_COUNT + frozen.descriptor);
            break;
          case ReferenceView::BUFFER_UAV: {
            const D3D12_UNORDERED_ACCESS_VIEW_DESC view = {.Format = frozen.format, .ViewDimension = D3D12_UAV_DIMENSION_BUFFER, .Buffer = {.NumElements = frozen.elements}};
            device_->CreateUnorderedAccessView(frozen.copy.Get(), nullptr, &view, slot);
            break;
          }
        }
      }
      for (D3D12_RESOURCE_BARRIER& barrier : to_copy) std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
      after.insert(after.end(), to_copy.begin(), to_copy.end());
      list->ResourceBarrier(static_cast<UINT>(after.size()), after.data());
      // b0 (the last trace's: its flags, the sun's cone), b3 and b4 as the request had them.
      run.frozen_settings = run.trace_settings;
      run.frozen_mesh_hash = run.mesh_hash;
      run.constants = Buffer(256 + LIGHT_CONSTANT_BYTES, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
      uint8_t* constants = nullptr;
      if (!run.constants || FAILED(run.constants->Map(0, nullptr, reinterpret_cast<void**>(&constants)))) {
        run.phase = ReferencePhase::OFF;
        slow_report_ += "\n[TW reference] no memory for its frozen constants: off";
        return;
      }
      std::memcpy(constants, call.path_constants, sizeof(PathConstants));
      std::memcpy(constants + 256, call.light_data, LIGHT_CONSTANT_BYTES);
      run.constants->Unmap(0, nullptr);
      std::array<uint32_t, 12> hashed_settings = run.frozen_settings;
      hashed_settings[0] = 0;
      run.frozen_constants.assign(reinterpret_cast<const uint8_t*>(hashed_settings.data()), reinterpret_cast<const uint8_t*>(hashed_settings.data() + 12));
      run.frozen_constants.insert(run.frozen_constants.end(), reinterpret_cast<const uint8_t*>(call.path_constants),
                                  reinterpret_cast<const uint8_t*>(call.path_constants + 1));
      run.frozen_constants.insert(run.frozen_constants.end(), call.light_data, call.light_data + LIGHT_CONSTANT_BYTES);
      state_op(REF_OP_DIGEST_FREEZE, 0, 0);
      run.phase = ReferencePhase::SLICES;
      return;
    }
    // A slice on the frozen inputs (their table, the frozen b0, b3, b4; b1 the slice's words, b2 the variant's parameters).
    const uint32_t per_group = std::max(run.total_samples / run.groups, 1u);
    const uint32_t group_slices = (per_group + run.slice_samples - 1) / run.slice_samples;
    const uint32_t slice = index - run.frames;
    const uint32_t group = slice / group_slices;
    const uint32_t first = (slice % group_slices) * run.slice_samples;
    const uint32_t samples = std::min(run.slice_samples, per_group - first);
    list->SetComputeRootDescriptorTable(ROOT_TABLE, frozen_table);
    list->SetComputeRoot32BitConstants(ROOT_SETTINGS, static_cast<UINT>(run.frozen_settings.size()), run.frozen_settings.data(), 0);
    list->SetComputeRootConstantBufferView(ROOT_PATHS, run.constants->GetGPUVirtualAddress());
    list->SetComputeRootConstantBufferView(ROOT_LIGHTS, run.constants->GetGPUVirtualAddress() + 256);
    const std::array<float, 20> parameters = {run.furnace[0], run.furnace[1], 0.f, 0.f, run.scale};
    list->SetComputeRoot32BitConstants(ROOT_BOUNCE, static_cast<UINT>(parameters.size()), parameters.data(), 0);
    grid_pass(((run.effect == ReferenceEffect::PATHS) ? run.paths : run.reflect).Get(),
              {run.variant | ((run.scale != 1.f) ? REF_SCALED : 0u) | (run.direct ? REF_DIRECT : 0u) | (run.code_break << REF_BREAK_SHIFT)
                   | ((REF_INSIDE_MODES[run.inside_mode] == "dark") ? REF_INSIDE_DARK : 0u),
               samples, first, group,
               per_group, run.seed, grid[0], grid[1]});
    if (first + samples >= per_group) {
      grid_pass(run.group.Get(), {group, 0u, 0u, run.pictures ? 1u : 0u, 0u, 0u, grid[0], grid[1]});
      state_op(REF_OP_GROUP_DONE, group, 0);
    }
    if (first + samples >= per_group && group + 1 >= run.groups) {
      // The last slice: the reference's census, then every sum, the state and the frozen inputs into a readback buffer.
      for (const uint32_t output : {8u, 10u, 11u}) grid_pass(run.census.Get(), {output, 0u, 0u, 0u, 0u, 0u, grid[0], grid[1]});
      run.reads.clear();
      const std::array<std::pair<const char*, UINT>, 10> outputs = {{{"candidate_shift", REF_CANDIDATE_SHIFT},
                                                                     {"candidate_mean", REF_CANDIDATE_MEAN},
                                                                     {"candidate_m2", REF_CANDIDATE_M2},
                                                                     {"candidate_cross", REF_CANDIDATE_CROSS},
                                                                     {"candidate_previous", REF_CANDIDATE_PREVIOUS},
                                                                     {"candidate_last", REF_CANDIDATE_LAST},
                                                                     {"ref_mean", REF_MEAN},
                                                                     {"ref_m2", REF_M2},
                                                                     {"ref_aux", REF_AUX},
                                                                     {"state", REF_STATE}}};
      for (const auto& [name, output] : outputs) {
        run.reads.push_back({.name = name, .resource = run.outputs[output - REF_CANDIDATE_BATCH].Get(), .state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS, .written = true});
      }
      if (run.pictures) {
        run.reads.push_back({.name = "ref_groups", .resource = run.outputs[REF_GROUPS - REF_CANDIDATE_BATCH].Get(), .state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                             .written = true});
      }
      for (const ReferenceFrozen& frozen : run.frozen) {
        const bool uav = frozen.view == ReferenceView::TEXTURE_UAV || frozen.view == ReferenceView::BUFFER_UAV;
        run.reads.push_back({.name = std::string("frozen_") + frozen.name, .resource = frozen.copy.Get(),
                             .state = uav ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, .written = frozen.written});
      }
      uint64_t total = 0;
      for (ReferenceRead& read : run.reads) {
        const D3D12_RESOURCE_DESC desc = read.resource->GetDesc();
        total = Align(total, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
        if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
          read.footprints = {{.Offset = total, .Footprint = {.Width = static_cast<UINT>(desc.Width), .Height = 1, .Depth = 1,
                                                             .RowPitch = static_cast<UINT>(desc.Width)}}};
          read.rows = {1};
          read.row_bytes = {desc.Width};
          total += desc.Width;
          continue;
        }
        const UINT subresources = desc.DepthOrArraySize;
        read.footprints.resize(subresources);
        read.rows.resize(subresources);
        read.row_bytes.resize(subresources);
        uint64_t bytes = 0;
        device_->GetCopyableFootprints(&desc, 0, subresources, total, read.footprints.data(), read.rows.data(), read.row_bytes.data(), &bytes);
        total += bytes;
      }
      run.readback = Buffer(std::max<uint64_t>(total, 256), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
      if (!run.readback) {
        run.phase = ReferencePhase::OFF;
        slow_report_ += "\n[TW reference] no memory for its readback: off";
        list->SetComputeRootDescriptorTable(ROOT_TABLE, live_table);
        return;
      }
      std::vector<D3D12_RESOURCE_BARRIER> to_read;
      for (const ReferenceRead& read : run.reads) to_read.push_back(Transition(read.resource, read.state, D3D12_RESOURCE_STATE_COPY_SOURCE));
      list->ResourceBarrier(static_cast<UINT>(to_read.size()), to_read.data());
      for (const ReferenceRead& read : run.reads) {
        if (read.resource->GetDesc().Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
          list->CopyBufferRegion(run.readback.Get(), read.footprints[0].Offset, read.resource, 0, read.row_bytes[0]);
          continue;
        }
        for (UINT sub = 0; sub < read.footprints.size(); ++sub) {
          const D3D12_TEXTURE_COPY_LOCATION to = {.pResource = run.readback.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, .PlacedFootprint = read.footprints[sub]};
          const D3D12_TEXTURE_COPY_LOCATION from = {.pResource = read.resource, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, .SubresourceIndex = sub};
          list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        }
      }
      for (D3D12_RESOURCE_BARRIER& barrier : to_read) std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
      list->ResourceBarrier(static_cast<UINT>(to_read.size()), to_read.data());
      run.phase = ReferencePhase::DUMPING;
      run.dump_value = own_value_ + 1;
    }
    // The request's own bindings back.
    list->SetComputeRootDescriptorTable(ROOT_TABLE, live_table);
    list->SetComputeRoot32BitConstants(ROOT_SETTINGS, static_cast<UINT>(call.settings->size()), call.settings->data(), 0);
    list->SetComputeRoot32BitConstants(ROOT_BOUNCE, static_cast<UINT>(call.bounce_constants->size()), call.bounce_constants->data(), 0);
    list->SetComputeRootConstantBufferView(ROOT_PATHS, call.paths);
    list->SetComputeRootConstantBufferView(ROOT_LIGHTS, call.lights);
  }
#endif

  // The world radiance cache and the learned lighting (rt_path.hlsli), made at the first path request (zeroed: empty).
  bool EnsureCache() {
    if (cache_keys_) return true;
    const auto pool = [&](DXGI_FORMAT format, UINT elements, UINT element_bytes, ComPtr<ID3D12Resource>* buffer, UINT descriptor) {
      *buffer = Buffer(uint64_t{elements} * element_bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      if (!*buffer) return false;
      const D3D12_UNORDERED_ACCESS_VIEW_DESC view = {.Format = format, .ViewDimension = D3D12_UAV_DIMENSION_BUFFER, .Buffer = {.NumElements = elements}};
      device_->CreateUnorderedAccessView(buffer->Get(), nullptr, &view, Descriptor(descriptor));
      return true;
    };
    if (pool(DXGI_FORMAT_R32_UINT, CACHE_ENTRIES, 4, &cache_keys_, CACHE_KEYS)
        && pool(DXGI_FORMAT_R32G32B32A32_FLOAT, CACHE_ENTRIES, 16, &cache_radiance_, CACHE_RADIANCE)
        && pool(DXGI_FORMAT_R32_UINT, CACHE_ENTRIES * 4, 4, &cache_sums_, CACHE_SUMS)
        && pool(DXGI_FORMAT_R32G32_UINT, CACHE_ENTRIES, 8, &cache_albedo_, CACHE_ALBEDO)
        && pool(DXGI_FORMAT_R32_UINT, 16, 4, &light_fit_, LIGHT_FIT)
        && pool(DXGI_FORMAT_R32G32B32A32_FLOAT, LIGHT_KEPT_TEXELS, 16, &light_kept_, LIGHT_KEPT)) {
      kept_readback_ = Buffer(uint64_t{LIGHT_KEPT_TEXELS} * 16, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
      // The fill's cell owners (ROOT_SCRATCH; zeroed: none, and cleared by the resolve each frame: not recorded).
      cache_owners_ = Buffer(uint64_t{CACHE_ENTRIES} * 4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      if (cache_owners_) return true;
    }
    cache_keys_.Reset();
    WriteNullPathDescriptors();
    return false;
  }

  // A texture of the helper's own at the shared textures' size (over `divisor`, rounded up; `extra_rows` more), and a
  // UAV of one at `descriptor`.
  bool Texture(DXGI_FORMAT format, ComPtr<ID3D12Resource>* resource, D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               UINT divisor = 1, UINT extra_rows = 0) const {
    const D3D12_RESOURCE_DESC positions = positions_->GetDesc();
    const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
    const D3D12_RESOURCE_DESC desc = {.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D, .Width = (positions.Width + divisor - 1) / divisor,
                                      .Height = ((positions.Height + divisor - 1) / divisor) + extra_rows,
                                      .DepthOrArraySize = 1, .MipLevels = 1, .Format = format, .SampleDesc = {.Count = 1},
                                      .Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS};
    return SUCCEEDED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(resource->GetAddressOf())));
  }
  void Written(ID3D12Resource* resource, DXGI_FORMAT format, UINT descriptor) const {
    const D3D12_UNORDERED_ACCESS_VIEW_DESC view = {.Format = format, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
    device_->CreateUnorderedAccessView(resource, nullptr, &view, Descriptor(descriptor));
  }

#if defined(RENODX_NRD)
  // A signal NRD denoises: made at `width` x `height` when first asked for (and again at a new size), kept while asked
  // for (its memory back NRD_RELEASE_FRAMES after: TraceScene); false (once said in the log): the built-in ways instead.
  bool EnsureNrd(NrdSignal* signal, nrd::Denoiser denoiser, const char* name, D3D12_SHADER_BYTECODE front, D3D12_SHADER_BYTECODE finish, uint32_t width, uint32_t height,
                 std::span<const NrdTexture> textures, uint32_t frame) {
    signal->used_frame = frame;
    if (signal->denoiser && signal->size == std::array<uint32_t, 2>{width, height}) return true;
    if (signal->failed) return false;
    if (signal->denoiser) {
      if (!WaitFor(own_value_)) return false;
      signal->denoiser.reset();
    }
    const auto fail = [&](const std::string& why) {
      signal->failed = true;
      slow_report_ += std::format("{}[TW ray tracing] {}: {}", slow_report_.empty() ? "" : "\n", name, why);
      return false;
    };
    // Test switch: NRD refused (the harness's nrd_refused modes).
    if (GetEnvironmentVariableW(L"RENODX_TW_TEST_NRD_REFUSED", nullptr, 0) != 0) return fail("refused (test)");
    // Its pipelines and textures' time (a cold shader cache compiles them here, inside a request).
    const auto started = std::chrono::steady_clock::now();
    if (!signal->front) {
      const D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {.pRootSignature = root_signature_.Get(), .CS = front};
      if (FAILED(device_->CreateComputePipelineState(&desc, IID_PPV_ARGS(signal->front.GetAddressOf())))) return fail("NRD's inputs' pipeline failed");
#if defined(RENODX_RECORDER)
      KeepCode(signal->front.Get(), front.pShaderBytecode, front.BytecodeLength);
#endif
    }
    auto made = std::make_unique<Nrd12>();
    std::string error;
    if (!made->Create(device_.Get(), denoiser, SLOTS, width, height, finish.pShaderBytecode, finish.BytecodeLength, &error)) return fail(error);
    // Motion: none but the camera's (a texel: NRD scales what it reads by 0).
    if (made->Add(nrd::ResourceType::IN_MV, DXGI_FORMAT_R16G16_FLOAT, 1u << 16) == nullptr) return fail("no memory");
    for (const NrdTexture& texture : textures) {
      ID3D12Resource* const resource = made->Add(texture.type, texture.format);
      if (resource == nullptr) return fail("no memory");
      if (texture.descriptor != 0) Written(resource, texture.format, texture.descriptor);
    }
    const auto made_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
#if defined(RENODX_RECORDER)
    // The recorder's: NRD's frame time fixed in a recording helper's scene and a play-back; a play-back's denoiser of a
    // recorded signal takes the recording's history and last frame (once, at the recorded size).
    if (journal_on_ || player_) made->fixed_frame_ms = 16.66f;
    const std::array<NrdSignal*, 4> signals = NrdSignals();
    if (std::optional<RecordedNrd>& recorded = recorded_nrd_[std::ranges::find(signals, signal) - signals.begin()]; recorded) {
      const auto history = made->History();
      const bool fits = recorded->size == std::array<uint32_t, 2>{width, height} && history.size() == recorded->history.size();
      ID3D12GraphicsCommandList4* list = fits ? RecorderList() : nullptr;
      std::vector<ComPtr<ID3D12Resource>> uploads;
      std::string why = fits ? std::string()
                             : std::format("recorded {} textures at {} x {}, made {} at {} x {}", recorded->history.size(), recorded->size[0],
                                           recorded->size[1], history.size(), width, height);
      bool restored = list != nullptr;
      for (size_t index = 0; restored && index < history.size(); ++index) {
        restored = UploadRecorded(list, name, history[index].first, history[index].second, recorded->history[index], &uploads, &why);
      }
      restored = list != nullptr && WaitFor(SubmitRecorderList(list)) && restored;
      if (restored) made->Restore(recorded->kept);
      slow_report_ += std::format("{}[TW player] {}: the recording's history {}.", slow_report_.empty() ? "" : "\n", name, restored ? "taken" : "NOT taken: " + why);
      recorded.reset();
    }
#endif
    slow_report_ += std::format("{}[TW ray tracing] {}: {} MB at {} x {}, made in {} ms.", slow_report_.empty() ? "" : "\n", name,
                                made->Bytes() >> 20, width, height, made_ms);
    signal->denoiser = std::move(made);
    signal->size = {width, height};
    return true;
  }
#endif

  // An effect's history (textures of its own at the textures' size, ~1.3 GB at 4K for all four), made when it is first
  // used and kept marked by `frame`; false: it could not be made.
  bool EnsureHistory(History kind, uint32_t frame) {
    history_used_[kind] = frame;
    switch (kind) {
      case HISTORY_PATHS:
        // Last frame's positions and this frame's (copied for the next): two slices, taking the copy in turn.
        if (kept_positions_) return true;
        {
          D3D12_RESOURCE_DESC desc = positions_->GetDesc();
          desc.DepthOrArraySize = 2;
          desc.MipLevels = 1;
          desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
          desc.Flags = D3D12_RESOURCE_FLAG_NONE;
          const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
          if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                      IID_PPV_ARGS(kept_positions_.GetAddressOf())))) {
            return false;
          }
          D3D12_SHADER_RESOURCE_VIEW_DESC view = NO_KEPT_POSITIONS;
          view.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
          device_->CreateShaderResourceView(kept_positions_.Get(), &view, Descriptor(PREVIOUS_POSITIONS));
        }
        copied_frame_ = UINT32_MAX;
        paths_copied_frame_ = UINT32_MAX;
        paths_frame_ = UINT32_MAX;
        trace_positions_slice_ = NO_SLICE;
        return true;
      case HISTORY_REUSE:
        // ReSTIR GI's two sets of reservoirs (the path-traced Light Bounce's) and the bounce texels' surfaces (rt_path.hlsli
        // surface_origin, surface_normal: this frame's only; ~265 MB at 4K).
        if (reuse_samples_[0]) return true;
        for (UINT set = 0; set < 2; ++set) {
          if (!Texture(RESERVOIR_POINTS, &reuse_samples_[set]) || !Texture(RESERVOIR_DATA, &reuse_data_[set])
              || !Texture(DXGI_FORMAT_R32G32B32A32_FLOAT, &reuse_surface_[set])) {
            reuse_samples_ = {};
            reuse_data_ = {};
            reuse_surface_ = {};
            return false;
          }
          Written(reuse_samples_[set].Get(), RESERVOIR_POINTS, REUSE_SAMPLES + set);
          Written(reuse_data_[set].Get(), RESERVOIR_DATA, REUSE_DATA + set);
        }
        Written(reuse_surface_[0].Get(), DXGI_FORMAT_R32G32B32A32_FLOAT, SURFACE_ORIGIN);
        Written(reuse_surface_[1].Get(), DXGI_FORMAT_R32G32B32A32_FLOAT, SURFACE_NORMAL);
        reuse_frame_ = UINT32_MAX;
        return true;
      case HISTORY_SKY:
        // Path-Traced Ambient's own sky, two sets (~130 MB at 4K).
        if (own_sky_[0]) return true;
        for (UINT set = 0; set < 2; ++set) {
          if (!Texture(DXGI_FORMAT_R16G16B16A16_FLOAT, &own_sky_[set])) {
            own_sky_ = {};
            return false;
          }
          Written(own_sky_[set].Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, OWN_SKY + set);
        }
        sky_frame_ = UINT32_MAX;
        return true;
      case HISTORY_RATIO:
        // Torch and Fire Shadows from the Light Bounce's shadow ratio, two sets (~130 MB at 4K, Full).
        if (light_ratio_[0]) return true;
        for (UINT set = 0; set < 2; ++set) {
          if (!Texture(DXGI_FORMAT_R16G16B16A16_FLOAT, &light_ratio_[set])) {
            light_ratio_ = {};
            return false;
          }
          Written(light_ratio_[set].Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, LIGHT_RATIO + set);
        }
        ratio_frame_ = UINT32_MAX;
        return true;
      case HISTORY_REFLECT:
        // Own ReSTIR's two sets (half the render pixels: ~130 MB at 4K).
        if (reflect_samples_[0]) return true;
        for (UINT set = 0; set < 2; ++set) {
          if (!Texture(RESERVOIR_POINTS, &reflect_samples_[set], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 2)
              || !Texture(RESERVOIR_DATA, &reflect_data_[set], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 2)) {
            reflect_samples_ = {};
            reflect_data_ = {};
            return false;
          }
          Written(reflect_samples_[set].Get(), RESERVOIR_POINTS, REFLECT_SAMPLES + set);
          Written(reflect_data_[set].Get(), RESERVOIR_DATA, REFLECT_DATA + set);
        }
        reflect_frame_ = UINT32_MAX;
        return true;
      case HISTORY_WATER:
        // Softer Water Reflections: the positions at the last reflections request that had the water's (~130 MB at 4K).
        if (water_positions_) return true;
        {
          D3D12_RESOURCE_DESC desc = positions_->GetDesc();
          desc.DepthOrArraySize = 1;
          desc.MipLevels = 1;
          desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
          desc.Flags = D3D12_RESOURCE_FLAG_NONE;
          const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
          if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                      IID_PPV_ARGS(water_positions_.GetAddressOf())))) {
            return false;
          }
        }
        water_frame_ = UINT32_MAX;
        return true;
      case HISTORY_SPECULAR:
        // The reflections' accumulation and its metadata, this frame's and the last.
        if (specular_accumulated_) return true;
        if (!Texture(DXGI_FORMAT_R16G16B16A16_FLOAT, &specular_accumulated_) || !Texture(DXGI_FORMAT_R16G16B16A16_FLOAT, &specular_history_)
            || !Texture(DXGI_FORMAT_R16G16_FLOAT, &specular_meta_) || !Texture(DXGI_FORMAT_R16G16_FLOAT, &specular_meta_history_)) {
          for (auto* texture : {std::addressof(specular_accumulated_), std::addressof(specular_history_), std::addressof(specular_meta_),
                                std::addressof(specular_meta_history_)}) {
            texture->Reset();
          }
          return false;
        }
        Written(specular_accumulated_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, SPECULAR_ACCUMULATED);
        Written(specular_history_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, SPECULAR_HISTORY);
        Written(specular_meta_.Get(), DXGI_FORMAT_R16G16_FLOAT, SPECULAR_META);
        Written(specular_meta_history_.Get(), DXGI_FORMAT_R16G16_FLOAT, SPECULAR_META_HISTORY);
        specular_history_valid_ = false;
        return true;
      case HISTORY_AMBIENT:
        // The ambient visibility's (v11): two sets in turn, the last trace's positions.
        if (ambient_positions_) return true;
        if (!Texture(DXGI_FORMAT_R16_FLOAT, &ambient_history_[0]) || !Texture(DXGI_FORMAT_R16_FLOAT, &ambient_history_[1])
            || !Texture(DXGI_FORMAT_R32G32B32A32_FLOAT, &ambient_positions_, D3D12_RESOURCE_STATE_COMMON)) {
          ambient_history_ = {};
          ambient_positions_.Reset();
          return false;
        }
        Written(ambient_history_[0].Get(), DXGI_FORMAT_R16_FLOAT, AMBIENT_HISTORY);
        Written(ambient_history_[1].Get(), DXGI_FORMAT_R16_FLOAT, AMBIENT_HISTORY + 1);
        Written(ambient_positions_.Get(), DXGI_FORMAT_R32G32B32A32_FLOAT, AMBIENT_POSITIONS);
        ambient_frame_ = UINT32_MAX;
        trace_positions_slice_ = NO_SLICE;
        return true;
      case HISTORY_LIGHTS:
        // The local lights' bits: two sets in turn (the ambient's parity, its positions), their tiles' lists below them
        // (v31, as the light mask's).
        if (light_history_[1]) return true;
        if (const UINT rows = dlaa_bridge::LightTileRows(static_cast<uint32_t>(positions_->GetDesc().Height));
            !Texture(DXGI_FORMAT_R32_UINT, &light_history_[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 1, rows)
            || !Texture(DXGI_FORMAT_R32_UINT, &light_history_[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 1, rows)) {
          light_history_ = {};
          return false;
        }
        Written(light_history_[0].Get(), DXGI_FORMAT_R32_UINT, LIGHT_HISTORY);
        Written(light_history_[1].Get(), DXGI_FORMAT_R32_UINT, LIGHT_HISTORY + 1);
        lights_frame_ = UINT32_MAX;
        return true;
      default:
        return false;
    }
  }

  // The history an effect left unused for HISTORY_FRAMES given back once the GPU is past this request (its views null:
  // nothing reads them while unused), logged.
  void ExpireHistory(uint32_t frame) {
    const D3D12_UNORDERED_ACCESS_VIEW_DESC null_written = {.Format = DXGI_FORMAT_R8G8B8A8_UNORM, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
    const auto release = [&](const char* effect, std::initializer_list<ComPtr<ID3D12Resource>*> textures, std::initializer_list<UINT> written) {
      for (auto* texture : textures) {
        if (*texture) releases_.push_back({.value = own_value_ + 1, .resource = std::move(*texture)});
      }
      for (const UINT descriptor : written) device_->CreateUnorderedAccessView(nullptr, nullptr, &null_written, Descriptor(descriptor));
      if (details_) {
        slow_report_ += std::format("{}[TW ray tracing] The {} history released (unused for {} frames).", (slow_report_.empty() ? "" : "\n"), effect,
                                    HISTORY_FRAMES);
      }
    };
    const auto unused = [&](History kind) { return history_used_[kind] + HISTORY_FRAMES < frame; };
    if (kept_positions_ && unused(HISTORY_PATHS)) {
      device_->CreateShaderResourceView(nullptr, &NO_KEPT_POSITIONS, Descriptor(PREVIOUS_POSITIONS));
      release("paths' positions", {std::addressof(kept_positions_)}, {});
      trace_positions_slice_ = NO_SLICE;
    }
    if (water_positions_ && unused(HISTORY_WATER)) release("water's positions", {std::addressof(water_positions_)}, {});
    if (reuse_samples_[0] && unused(HISTORY_REUSE)) {
      release("ReSTIR GI", {std::addressof(reuse_samples_[0]), std::addressof(reuse_samples_[1]), std::addressof(reuse_data_[0]), std::addressof(reuse_data_[1]),
                            std::addressof(reuse_surface_[0]), std::addressof(reuse_surface_[1])},
              {REUSE_SAMPLES, REUSE_SAMPLES + 1, REUSE_DATA, REUSE_DATA + 1, SURFACE_ORIGIN, SURFACE_NORMAL});
    }
    if (own_sky_[0] && unused(HISTORY_SKY)) release("own sky's", {std::addressof(own_sky_[0]), std::addressof(own_sky_[1])}, {OWN_SKY, OWN_SKY + 1});
    // Torch Light in Path-Traced GI's torch light and ReGIR cells: their row's paths' frame (no history kind of their own).
    if ((torch_irradiance_ || regir_cells_) && (torch_light_frame_ == UINT32_MAX || torch_light_frame_ + HISTORY_FRAMES < frame)) {
      release("torch light's", {std::addressof(torch_irradiance_), std::addressof(regir_cells_)}, {TORCH_IRRADIANCE});
      device_->CreateUnorderedAccessView(nullptr, nullptr, &NO_REGIR_CELLS, Descriptor(REGIR_CELLS));
    }
    if (light_ratio_[0] && unused(HISTORY_RATIO)) {
      release("torch shadow ratio's", {std::addressof(light_ratio_[0]), std::addressof(light_ratio_[1])}, {LIGHT_RATIO, LIGHT_RATIO + 1});
    }
    if (reflect_samples_[0] && unused(HISTORY_REFLECT)) {
      release("Own ReSTIR", {std::addressof(reflect_samples_[0]), std::addressof(reflect_samples_[1]), std::addressof(reflect_data_[0]), std::addressof(reflect_data_[1])},
              {REFLECT_SAMPLES, REFLECT_SAMPLES + 1, REFLECT_DATA, REFLECT_DATA + 1});
    }
    if (specular_accumulated_ && unused(HISTORY_SPECULAR)) {
      release("reflections'", {std::addressof(specular_accumulated_), std::addressof(specular_history_), std::addressof(specular_meta_),
               std::addressof(specular_meta_history_)},
              {SPECULAR_ACCUMULATED, SPECULAR_HISTORY, SPECULAR_META, SPECULAR_META_HISTORY});
    }
    if (ambient_positions_ && unused(HISTORY_AMBIENT)) {
      release("ambient occlusion's", {std::addressof(ambient_history_[0]), std::addressof(ambient_history_[1]), std::addressof(ambient_positions_)},
              {AMBIENT_HISTORY, AMBIENT_HISTORY + 1, AMBIENT_POSITIONS});
    }
    if (light_history_[1] && unused(HISTORY_LIGHTS)) {
      release("local lights'", {std::addressof(light_history_[0]), std::addressof(light_history_[1])}, {LIGHT_HISTORY, LIGHT_HISTORY + 1});
    }
  }

  // A pass's name among the list's DRED breadcrumbs, kept with its slot (LostReport: DRED leaves markers unnamed).
  void Mark(ID3D12GraphicsCommandList* list, const wchar_t* pass) {
#if defined(RENODX_RECORDER)
    ServeMark(list, pass);
#endif
    if (!details_) return;
    if (profiling_) QueryPerformanceCounter(&cpu_marks_.emplace_back(pass, LARGE_INTEGER{}).second);
    list->SetMarker(0, pass, static_cast<UINT>((std::wcslen(pass) + 1) * sizeof(wchar_t)));
    for (Slot& slot : slots_) {
      if (slot.list.Get() == list) slot.marks.push_back(pass);
    }
  }

  // A slot's passes by Mark's names ("none" without Log Details): a request whose list could not be closed.
  static std::string Passes(const Slot& slot) {
    std::string passes;
    for (const wchar_t* pass : slot.marks) {
      passes += passes.empty() ? "" : ", ";
      for (; *pass != L'\0'; ++pass) passes += static_cast<char>(*pass);
    }
    return passes.empty() ? "none" : passes;
  }

  static D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    return {
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Transition = {.pResource = resource, .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, .StateBefore = before, .StateAfter = after},
    };
  }

  // The request's commands into the slot's list (closed); false: nothing usable recorded.
  bool Record(const dlaa_bridge::Shared& shared, uint32_t version, std::span<const uint8_t> cluster_data, size_t slot_index, Slot* slot) {
    auto* list = slot->list.Get();
    // A request that cannot be recorded: its list closed, a new scene next (Lost; unlike Fail: ATN's debug32, one bad request
    // then left ray tracing off once the textures were made again).
    const auto skip = [&](std::string why) {
      list->Close();
      request_failed_ = true;
      status_ = std::move(why);
      return false;
    };
    StampSlot(list, slot_index);
#if defined(RENODX_RECORDER)
    ServeStamp(list, SERVE_START);
#endif
    Stamp(list, slot_index * STAMPS);
    const uint32_t frame = shared.rt_frame;
    const D3D12_RESOURCE_BARRIER uav_barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV};
    ID3D12DescriptorHeap* heaps[] = {descriptors_.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(root_signature_.Get());
    list->SetComputeRootDescriptorTable(ROOT_TABLE, descriptors_->GetGPUDescriptorHandleForHeapStart());
    // The last trace's placed instances past 3/4 of their room: twice it or more, up to the GPU's limit, made on a worker and
    // put in at the first request after, with the kept descs and the counters (the last trace is done; paths requests on
    // their way hold the old TLAS until the release's fence value). No memory for it: a limit half as high (the room stays).
#if defined(RENODX_RECORDER)
    if (Lockstep() && placed_growing_.valid()) placed_growing_.wait();
#endif
    if (placed_growing_.valid() && placed_growing_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      if (const std::unique_ptr<PlacedRoom> made = placed_growing_.get(); made) {
        ID3D12Resource* carried = made->instances[placed_last_].Get();
        ID3D12Resource* kept = placed_instances_[placed_last_].Get();
        const std::array<D3D12_RESOURCE_BARRIER, 4> to_carry = {
            Transition(kept, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
            Transition(carried, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
            Transition(placed_state_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
            Transition(made->state.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST),
        };
        list->ResourceBarrier(static_cast<UINT>(to_carry.size()), to_carry.data());
        if (placed_written_ != 0) {
          list->CopyBufferRegion(carried, 0, kept, 0, uint64_t{std::min(placed_written_, placed_capacity_)} * sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
        }
        list->CopyBufferRegion(made->state.Get(), 0, placed_state_.Get(), 0, 16);
        const std::array<D3D12_RESOURCE_BARRIER, 2> carried_back = {
            Transition(carried, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
            Transition(made->state.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        };
        list->ResourceBarrier(static_cast<UINT>(carried_back.size()), carried_back.data());
#if defined(RENODX_RECORDER)
        // The new room's copies: read under its placed names from here on.
        if (uses_) {
          uses_->External(UseResource(carried));
          uses_->External(UseResource(made->state.Get()));
        }
#endif
        UsePlacedRoom(made.get());
        if (details_) {
          slow_report_ += std::format("{}[TW ray tracing] Room for {} placed instances now ({} written).", (slow_report_.empty() ? "" : "\n"), placed_capacity_, placed_written_);
        }
      } else {
        placed_limit_ = std::max(placed_capacity_, placed_limit_ / 2);
        room_note_ = std::format("No memory: {} placed instances at most | ", placed_limit_);
        slow_report_ += std::format("{}[TW ray tracing] {}.", (slow_report_.empty() ? "" : "\n"), room_note_.substr(0, room_note_.size() - 3));
      }
    }
    if (!placed_growing_.valid() && uint64_t{placed_written_} * 4 > uint64_t{placed_capacity_} * 3 && placed_capacity_ < placed_limit_) {
      const auto capacity = static_cast<uint32_t>(std::min<uint64_t>(
          std::max<uint64_t>(uint64_t{placed_capacity_} * 2, std::bit_ceil(uint64_t{placed_written_} * 2)), placed_limit_));
      placed_growing_ = std::async(std::launch::async, [this, capacity]() -> std::unique_ptr<PlacedRoom> {
        auto made = std::make_unique<PlacedRoom>();
        if (!MakePlacedRoom(capacity, made.get())) return nullptr;
        return made;
      });
    }
    // Unused root descriptors still point at a buffer.
    list->SetComputeRootShaderResourceView(ROOT_CAPTURE_TABLE, slot->table->GetGPUVirtualAddress());
    for (const Root root : {ROOT_MICROMAP_DATA, ROOT_MICROMAP_INDICES, ROOT_MICROMAP_DESCS}) {
      list->SetComputeRootUnorderedAccessView(root, tlas_scratch_->GetGPUVirtualAddress());
    }
    // Instanced meshes captured at this slot's last trace: each instance's transform from the first one's space
    // (rows_k times the inverse of rows_0; degenerate rows: the first instance only).
    if (!slot->row_reads.empty()) {
      const D3D12_RANGE read = {.Begin = 0, .End = uint64_t{ROWS_READBACK} * 64};
      void* mapped = nullptr;
      if (SUCCEEDED(slot->rows_readback->Map(0, &read, &mapped))) {
        const auto* rows = static_cast<const std::array<float, 16>*>(mapped);
        for (const auto& row_read : slot->row_reads) {
          const auto mesh = meshes_.find(row_read.key);
          if (mesh == meshes_.end() || mesh->second.build != row_read.build) continue;
          const auto& first = rows[row_read.offset];
          // The first instance's 3 x 4 (rows acting on (position, 1)) inverted.
          const float a = first[0], b = first[1], c = first[2], d = first[4], e = first[5], f = first[6], g = first[8], h = first[9],
                      i = first[10];
          const float determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
          if (!(std::abs(determinant) > 1.000023e-12f)) continue;
          const std::array<float, 9> inverse = {(e * i - f * h) / determinant, (c * h - b * i) / determinant, (b * f - c * e) / determinant,
                                                (f * g - d * i) / determinant, (a * i - c * g) / determinant, (c * d - a * f) / determinant,
                                                (d * h - e * g) / determinant, (b * g - a * h) / determinant, (a * e - b * d) / determinant};
          const std::array<float, 3> moved = {first[3], first[7], first[11]};
          std::vector<std::array<float, 12>> transforms(row_read.count);
          bool finite = true;
          for (uint32_t instance = 0; instance < row_read.count; ++instance) {
            const auto& row = rows[row_read.offset + instance];
            auto& transform = transforms[instance];
            for (int r = 0; r < 3; ++r) {
              float translation = row[r * 4 + 3];
              for (int column = 0; column < 3; ++column) {
                const float value = row[r * 4] * inverse[column] + row[r * 4 + 1] * inverse[3 + column] + row[r * 4 + 2] * inverse[6 + column];
                transform[r * 4 + column] = value;
                translation -= value * moved[column];
              }
              transform[r * 4 + 3] = translation;
            }
            finite = finite && std::ranges::all_of(transform, [](float value) { return std::isfinite(value); });
          }
          uint32_t first_instance = 0;
          if (!finite || !free_instances_.Take(row_read.count, &first_instance)) continue;
          mesh->second.transforms = std::move(transforms);
          mesh->second.first_instance = first_instance;
        }
        const D3D12_RANGE written = {};
        slot->rows_readback->Unmap(0, &written);
      }
      slot->row_reads.clear();
    }
    // Compactions: the BLASes built at this slot's last trace (done) copied into places their compacted size (the
    // kept pool).
    if (!slot->compactions.empty()) {
      const D3D12_RANGE read = {.Begin = 0, .End = slot->compactions.size() * sizeof(uint64_t)};
      void* mapped = nullptr;
      if (SUCCEEDED(slot->readback->Map(0, &read, &mapped))) {
        const auto* sizes = static_cast<const uint64_t*>(mapped);
        for (size_t index = 0; index < slot->compactions.size(); ++index) {
          const auto mesh = meshes_.find(slot->compactions[index].first);
          if (mesh == meshes_.end() || mesh->second.build != slot->compactions[index].second || sizes[index] == 0
              || sizes[index] >= mesh->second.bytes) {
            continue;
          }
          BlasPlace compacted = PlaceBlas(sizes[index], KEPT_POOL);
          if (compacted.address == 0) break;
          list->CopyRaytracingAccelerationStructure(compacted.address, mesh->second.blas.address, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
          ReleaseBlas(&mesh->second.blas);
          mesh->second.blas = std::move(compacted);
          mesh->second.bytes = sizes[index];
        }
        const D3D12_RANGE written = {};
        slot->readback->Unmap(0, &written);
      }
      slot->compactions.clear();
      list->ResourceBarrier(1, &uav_barrier);
    }
    // A scratch arena for builds recorded together (one barrier after them; grown, the old one released once the GPU
    // is past this request).
    const auto ensure_scratch = [&](uint64_t bytes) {
      if (bytes <= scratch_bytes_) return true;
      if (scratch_) releases_.push_back({.value = own_value_ + 1, .resource = std::move(scratch_)});
      scratch_bytes_ = std::max(bytes, scratch_bytes_ * 2);
      scratch_ = Buffer(scratch_bytes_, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      if (!scratch_) scratch_bytes_ = 0;
      return static_cast<bool>(scratch_);
    };
    // New captures: their vertices copied from the shared texture, a place each (record, pool ranges), the scene data,
    // micromaps for new leaves (DXR 1.2), a BLAS each (built for tracing) and the units' one BLAS (refitted).
    uint32_t built = 0;
    uint32_t micromaps_built = 0;
    uint64_t dynamic_triangles = 0;
    uint64_t grass_triangles = 0;    // of them alpha-tested (grass, swaying leaves)
    uint64_t terrain_triangles = 0;  // and the terrain's
    bool units_built = false;
    bool units_refitted = false;
    bool units_clustered = false;  // Mega Geometry's cluster BLAS built for them
    bool smoke_built = false;      // the smoke's BLAS (SMOKE_INSTANCE)
    bool smoke_clustered = false;  // or its cluster BLAS
    std::array<bool, CLUSTER_BLASES> cluster_built = {};  // the cluster BLASes built (units_clustered: the first three's)
    uint32_t smoke_record = 0;     // its first capture's (or cluster's) record
    uint32_t clusters_built = 0;
    uint32_t templated_built = 0;  // of them instantiated from templates
    const uint32_t capture_count = std::min(shared.rt_capture_count, dlaa_bridge::RT_CAPTURES);
    // v14: this frame's draws of placed meshes in the scene (their instances' rows in the vertex texture).
    const uint32_t placement_count = (version >= 14) ? std::min(shared.rt_placement_count, dlaa_bridge::RT_PLACEMENTS) : 0u;
    // The request's vertices copied from the shared texture: the captures' triangles and rows, the placements' rows.
    uint32_t vertex_end = 0;
    for (uint32_t index = 0; index < capture_count; ++index) {
      const dlaa_bridge::RtCapture& capture = shared.rt_captures[index];
      const uint32_t rows = (version >= 10 && (capture.flags & dlaa_bridge::RT_INSTANCED) != 0) ? capture.instances * 4u : 0u;
      vertex_end = std::max(vertex_end, capture.first_vertex + capture.vertex_count + rows);
    }
    for (uint32_t index = 0; index < placement_count; ++index) {
      vertex_end = std::max(vertex_end, shared.rt_placements[index].first_vertex + shared.rt_placements[index].instances * 4u);
    }
    // Compact draws (v16, with Mega Geometry): the new cluster sets into the pool (one sent again replaces its last; the
    // pool full, the sets this request's draws do not use are dropped and the addon sends them again), then the draws
    // whose set is here and that fit this slot's turn: their table for the compact pass, their clusters first in the
    // units' cluster BLAS (records from first_cluster_record), their triangles first in the slot's pools.
    const uint32_t first_cluster_record = CLUSTER_RECORDS + (static_cast<uint32_t>(slot_index) * MAX_CLUSTERS);
    const uint32_t compact_count = (version >= 16 && clusters_) ? std::min(shared.rt_compact_count, dlaa_bridge::RT_COMPACT_DRAWS) : 0u;
    const uint32_t new_set_count = (version >= 16 && clusters_) ? std::min(shared.rt_new_set_count, dlaa_bridge::RT_NEW_SETS) : 0u;
    std::vector<std::array<uint64_t, 3>> set_copies;  // upload offset, pool offset, bytes
    // The sets this request's draws use (kept when a pool is full).
    const auto used_sets = [&] {
      std::unordered_set<uint64_t> used;
      for (uint32_t draw = 0; draw < compact_count; ++draw) used.insert(shared.rt_compact[draw].set);
      return used;
    };
    uint64_t set_upload = COMPACT_SETS_OFFSET;
    bool sets_dropped = false;
    for (uint32_t index = 0; index < new_set_count; ++index) {
      const dlaa_bridge::RtClusterSet& sent = shared.rt_new_sets[index];
      const uint64_t bytes = (uint64_t{sent.clusters} * 16) + (uint64_t{sent.triangles} * 3);
      if (sent.clusters == 0 || sent.vertices == 0 || uint64_t{sent.offset} + bytes > cluster_data.size() || set_upload + bytes > COMPACT_UPLOAD_BYTES) {
        continue;
      }
      // Each cluster within its block and set, each triangle's vertices within its cluster (a bad index would read past
      // the vertex buffer: a GPU fault), the clusters' vertices the block's (the CLASes' vertices: the request's texels).
      const uint8_t* data = cluster_data.data() + sent.offset;
      const uint8_t* corners = data + (uint64_t{sent.clusters} * 16);
      bool valid = true;
      uint64_t block = 0;
      std::vector<std::array<uint32_t, 4>> descs(sent.clusters);
      for (uint32_t cluster = 0; cluster < sent.clusters && valid; ++cluster) {
        std::array<uint32_t, 4>& desc = descs[cluster];
        std::memcpy(desc.data(), data + (uint64_t{cluster} * 16), sizeof(desc));
        const auto [first_vertex, vertices, first_triangle, triangles] = desc;
        valid = vertices != 0 && vertices <= dlaa_bridge::RT_CLUSTER_VERTICES && triangles != 0 && triangles <= dlaa_bridge::RT_CLUSTER_TRIANGLES
                && uint64_t{first_vertex} + vertices <= sent.vertices && uint64_t{first_triangle} + triangles <= sent.triangles
                && std::all_of(corners + (uint64_t{first_triangle} * 3), corners + ((uint64_t{first_triangle} + triangles) * 3),
                               [vertices](uint8_t corner) { return corner < vertices; });
        block += vertices;
      }
      if (!valid || block != sent.vertices) continue;
      const uint32_t units = static_cast<uint32_t>((bytes + 15) / 16);
      uint32_t offset = 0;
      if (!free_set_units_.Take(units, &offset)) {
        if (!sets_dropped) {
          const std::unordered_set<uint64_t> used = used_sets();
          std::erase_if(sets_, [&](const auto& entry) {
            if (used.contains(entry.first)) return false;
            pending_set_frees_.push_back({.value = own_value_ + 1, .offset = entry.second.offset, .units = entry.second.units,
                                          .template_first = entry.second.template_first, .templates = entry.second.clusters,
                                          .micromaps = std::move(entry.second.micromaps),
                                          .micromap_inputs = std::move(entry.second.micromap_inputs)});
            return true;
          });
          ++set_drops_;
          sets_dropped = true;
        }
        continue;
      }
      if (const auto last = sets_.find(sent.key); last != sets_.end()) {
        pending_set_frees_.push_back({.value = own_value_ + 1, .offset = last->second.offset, .units = last->second.units,
                                      .template_first = last->second.template_first, .templates = last->second.clusters,
                                      .micromaps = std::move(last->second.micromaps), .micromap_inputs = std::move(last->second.micromap_inputs)});
      }
      sets_[sent.key] = {.offset = offset, .units = units, .vertices = sent.vertices, .clusters = sent.clusters, .triangles = sent.triangles,
                         .descs = std::move(descs), .generation = ++set_generation_};
      std::memcpy(slot->compact_data + set_upload, data, bytes);
      set_copies.push_back({set_upload, uint64_t{offset} * 16, bytes});
      set_upload = Align(set_upload + bytes, 16);
    }
    if (!set_copies.empty()) {
      const auto to_copy = Transition(set_pool_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
      list->ResourceBarrier(1, &to_copy);
      for (const auto& [from, to, bytes] : set_copies) list->CopyBufferRegion(set_pool_.Get(), to, slot->compact.Get(), from, bytes);
      const auto copied = Transition(set_pool_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      list->ResourceBarrier(1, &copied);
    }
#if defined(RENODX_NVAPI_CLUSTERS)
    // The last request's clusters filled 3/4 of a run's room or more: that run's room half again what it was or a third
    // more than it wanted, so it is under 3/4 after (a quarter more grew it once more: 827 K clusters for 400 K, 7.6 GB in
    // the harness; in steps of CLUSTER_STEP; doubling the whole room made 524 K clusters for 166 K, 4.8 GB, ATN's
    // first profile; half again what it wanted grew it 12 times as the grass came into view, the second; twice the room
    // again 524 K for 186 K, the third), made on a worker and put in at the first request after (no memory for it: the
    // tier below, the room stays).
    const auto clusters_tier_below = [&] {
      const auto below = std::ranges::find_if(CLUSTER_TIERS, [&](uint32_t tier) { return tier < cluster_limit_; });
      cluster_limit_ = std::max(cluster_room_, (below != CLUSTER_TIERS.end()) ? *below : cluster_room_);
      room_note_ = std::format("No memory: {} clusters at most | ", cluster_limit_);
      slow_report_ += std::format("{}[TW ray tracing] {}.", (slow_report_.empty() ? "" : "\n"), room_note_.substr(0, room_note_.size() - 3));
    };
#if defined(RENODX_RECORDER)
    if (Lockstep() && cluster_growing_.valid()) cluster_growing_.wait();
#endif
    if (cluster_growing_.valid() && cluster_growing_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      if (const std::unique_ptr<ClusterRoom> made = cluster_growing_.get(); made) {
        UseClusterRoom(made.get());
        if (details_) {
          slow_report_ += std::format("{}[TW ray tracing] Mega Geometry: room for {} clusters a frame now ({} MB).", (slow_report_.empty() ? "" : "\n"),
                                      cluster_room_, (cluster_bytes_ + TEMPLATE_POOL_BYTES) >> 20);
        }
      } else {
        clusters_tier_below();
      }
    }
    if (clusters_ && clusters_crowded_ && !cluster_growing_.valid() && cluster_room_ < cluster_limit_) {
      const auto grown = [](uint32_t room, uint64_t wanted) {
        if (wanted * 4 <= uint64_t{room} * 3) return room;
        const uint64_t step = std::max<uint64_t>(uint64_t{room} * 3 / 2, wanted * 4 / 3);
        return static_cast<uint32_t>(std::min<uint64_t>((step + CLUSTER_STEP - 1) / CLUSTER_STEP * CLUSTER_STEP, MAX_CLUSTERS));
      };
      // Within the GPU's limit neither run's room shrinks.
      const uint32_t templated = std::min(grown(cluster_room_ - built_limit_, templated_wanted_), cluster_limit_ - built_limit_);
      const uint32_t built = std::min(grown(built_limit_, built_wanted_), cluster_limit_ - templated);
      auto made = std::make_unique<ClusterRoom>();
      if (templated + built > cluster_room_) {
        if (SizeClusterRoom(templated + built, built, made.get())) {
          cluster_growing_ = std::async(std::launch::async, [this, made = std::move(made)]() mutable -> std::unique_ptr<ClusterRoom> {
            if (!MakeClusterRoom(made.get())) return nullptr;
            return std::move(made);
          });
        } else {
          clusters_tier_below();
        }
      }
    }
    clusters_crowded_ = false;
    templated_wanted_ = 0;
    built_wanted_ = 0;
#endif
    uint32_t dynamic_triangles_used = 0;  // this slot's turn of the normal and UV pools
    uint32_t dynamic_uvs_used = 0;
    uint64_t cluster_vertices = 0;  // the units' CLASes' (at most TRANSFER_VERTICES: clas_inputs_)
    uint32_t compact_drawn = 0;
    uint32_t compact_clusters = 0;
    bool compact_masked = false;
    // Their keys and flags (the garbage count's sources after the captures).
    std::vector<uint64_t> compact_keys;
    std::vector<uint32_t> compact_flags;
    // In four runs: the draws instantiated from their set's templates (a set's made at its first draw while there is room;
    // the pool full, the templates of the sets this request does not use are given back), the grass's last, then those
    // built from triangles (no templates yet; with the captures' clusters at most built_limit_), the grass's first: the
    // grass's clusters together (their own cluster BLAS, GRASS_INSTANCE). The table in that order: the compact pass finds a
    // cluster's draw by its first cluster.
    // Swaying Shadows Range / Off-Screen Sway (v18, rt_sway.hlsli): the compact pass leaves out the swaying copies of the
    // instances it turns down, the place pass shows their placed copies; only for swaying draws whose placement goes in
    // (their rows here: hidden, of a placed mesh, its rows copied). Grass Ray Tracing Range (in v31's padding: older addons
    // leave it 0, none): grass past it leaves both passes' scene, its swaying copies by the same cut.
    const float grass_range = (version >= 31) ? std::clamp(shared.rt_grass_range, 0.f, 1.0e4f) : 0.f;
    // Units Ray Tracing Range (in v7's padding: older addons leave it 0, none): RT_UNIT draws' instances past it leave the
    // compact pass's scene, in whole metres in the sway flags' high bits (the compact pass's constants are full). The
    // harness's RENODX_TW_UNITS_RANGE: that range for every request.
    static const float harness_units_range = [] {
      std::array<wchar_t, 32> value = {};
      return (GetEnvironmentVariableW(L"RENODX_TW_UNITS_RANGE", value.data(), static_cast<DWORD>(value.size())) != 0) ? std::wcstof(value.data(), nullptr) : 0.f;
    }();
    const uint32_t units_range =
        static_cast<uint32_t>(std::clamp((harness_units_range > 0.f) ? harness_units_range : ((version >= 31) ? shared.rt_units_range : 0.f), 0.f, 65535.f));
    // RT_OWN_CUTOFF's alpha cut-off (in v8's padding: older addons leave it 0, the 0.5 of every other alpha-tested draw); the
    // harness's RENODX_TW_OWN_CUTOFF: every alpha-tested draw RT_OWN_CUTOFF at that.
    static const float harness_cutoff = [] {
      std::array<wchar_t, 32> value = {};
      return (GetEnvironmentVariableW(L"RENODX_TW_OWN_CUTOFF", value.data(), static_cast<DWORD>(value.size())) != 0) ? std::wcstof(value.data(), nullptr) : 0.f;
    }();
    const uint32_t own_cutoff_flags = (harness_cutoff > 0.f) ? dlaa_bridge::RT_OWN_CUTOFF : 0u;
    const float own_cutoff = std::clamp((harness_cutoff > 0.f) ? harness_cutoff : ((shared.rt_leaf_cutoff > 0.f) ? shared.rt_leaf_cutoff : LEAF_ALPHA_CUTOFF), 0.f, 2.f);
    const auto cutoff_of = [own_cutoff, own_cutoff_flags](uint32_t flags) {
      return (((flags | own_cutoff_flags) & dlaa_bridge::RT_OWN_CUTOFF) != 0) ? own_cutoff : LEAF_ALPHA_CUTOFF;
    };
    uint32_t sway_flags = 0;
    std::unordered_set<uint32_t> sway_rows;
    if (sway_pipeline_ && version >= 18
        && (shared.rt_sway_range > 0.f || (shared.rt_flags & dlaa_bridge::RT_SWAY_IN_VIEW) != 0 || grass_range > 0.f || units_range != 0)
        && GrowBuffer(std::addressof(sway_arguments_), cluster_args_->GetDesc().Width, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
        && GrowBuffer(std::addressof(sway_lists_), clas_addresses_->GetDesc().Width, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        && GrowBuffer(std::addressof(sway_counts_), SWAY_COUNTS_BYTES, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)) {
      sway_flags = SWAY_CUT | (((shared.rt_flags & dlaa_bridge::RT_SWAY_IN_VIEW) != 0) ? SWAY_IN_VIEW : 0u);
      for (uint32_t index = 0; index < placement_count; ++index) {
        const dlaa_bridge::RtPlacement& placement = shared.rt_placements[index];
        const auto mesh = meshes_.find(placement.key);
        if ((placement.flags & dlaa_bridge::RT_PLACE_HIDDEN) != 0 && mesh != meshes_.end() && mesh->second.placed
            && uint64_t{placement.first_vertex} + (uint64_t{placement.instances} * 4) <= std::min(vertex_end, TRANSFER_VERTICES)) {
          sway_rows.insert(placement.first_vertex);
        }
      }
    }
    uint32_t templated_clusters = 0;  // the first two runs'
    uint32_t grass_first = 0;         // the grass's clusters
    uint32_t grass_end = 0;
    uint32_t template_builds = 0;     // this request's new templates
    bool templates_dropped = false;
    // Grass micromaps (with the Leaf Micromaps setting and Mega Geometry's micromap support): an alpha-tested set's first
    // instance's UVs read back once, a few sets a request (the bakes' room shared with the leaves').
    std::vector<MicromapRead> set_micromap_reads;
    // GPU Micromap Baking: every read baked on the GPU (none read back for the CPU), waiting for the requests' bakes
    // (at the end of each, within its budget).
    const bool gpu_bakes = gpu_micromaps_ && (shared.rt_flags & dlaa_bridge::RT_GPU_MICROMAPS) != 0;
    // RENODX_TW_OMM_CHURN=<frames>: every set baked again that often, the micromap path under load (finding GPU resets).
    static const uint32_t omm_churn = [] {
      std::array<wchar_t, 16> value = {};
      return (GetEnvironmentVariableW(L"RENODX_TW_OMM_CHURN", value.data(), static_cast<DWORD>(value.size())) != 0) ? static_cast<uint32_t>(_wtoi(value.data())) : 0u;
    }();
    if (omm_churn != 0 && frame % omm_churn == 0) {
      for (auto& [key, set] : sets_) set.micromaps_tried = false;
    }
    size_t set_micromap_room = (cluster_micromaps_ && bake_micromaps_ && dxr12_ && (shared.rt_flags & dlaa_bridge::RT_MICROMAPS) != 0)
                                   ? std::min<size_t>((gpu_bakes ? MICROMAP_GPU_BAKES : 0u) + SET_MICROMAP_READS,
                                                      MICROMAP_BAKES - std::min(MICROMAP_BAKES, micromap_reads_.size() + micromap_queue_.size()
                                                                                                    + MicromapsBaking() + micromaps_baked_.size()))
                                   : 0u;
#if defined(RENODX_RECORDER)
    // A snapshot waiting for the bakes in flight: no new ones meanwhile.
    if (SnapshotWaiting()) set_micromap_room = 0;
#endif
#if defined(RENODX_NVAPI_CLUSTERS)
    auto* template_args =
        reinterpret_cast<NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_TEMPLATE_ARGS*>(slot->compact_data + COMPACT_TEMPLATE_ARGS_OFFSET);
    auto* template_destinations = reinterpret_cast<uint64_t*>(slot->compact_data + COMPACT_TEMPLATE_DESTINATIONS_OFFSET);
    std::vector<ClusterSet*> new_templated_sets;  // the sets given templates in this request (sets_ is not changed until its end)
    bool templates_refused = false;
    // A Mega Geometry call NvAPI refused: once a kind (its bit in cluster_refusals_said_) in the slow report.
    const auto say_refused = [&](uint32_t kind, const std::string& line) {
      if ((cluster_refusals_said_ & kind) != 0) return;
      cluster_refusals_said_ |= kind;
      slow_report_ += std::format("{}[TW ray tracing] Mega Geometry: {}", (slow_report_.empty() ? "" : "\n"), line);
    };
#endif
    for (const auto [templated_run, grass_run] : {std::pair{true, false}, std::pair{true, true}, std::pair{false, true}, std::pair{false, false}}) {
      if (templated_run && grass_run) grass_first = compact_clusters;
      for (uint32_t index = 0; index < compact_count; ++index) {
        const dlaa_bridge::RtCompactDraw& draw = shared.rt_compact[index];
        const auto found = sets_.find(draw.set);
        if (found == sets_.end() || draw.instances == 0) continue;
        ClusterSet& set = found->second;
        const bool masked = (draw.flags & dlaa_bridge::RT_FOLIAGE) != 0 && atlas_;
        if ((masked && (draw.flags & dlaa_bridge::RT_GRASS) != 0) != grass_run) continue;
#if defined(RENODX_NVAPI_CLUSTERS)
        uint32_t first_template = 0;
        if (templated_run && set.template_first == UINT32_MAX && template_builds + set.clusters <= TEMPLATE_BUILDS) {
          if (free_templates_.Take(set.clusters, &first_template)) {
            set.template_first = first_template;
            set.template_masked = masked;
            new_templated_sets.push_back(&set);
            for (uint32_t cluster = 0; cluster < set.clusters; ++cluster) {
              const auto& [desc_vertex, desc_vertices, desc_triangle, desc_triangles] = set.descs[cluster];
              auto& arg = template_args[template_builds];
              arg = {};
              arg.triangleCount = desc_triangles;
              arg.vertexCount = desc_vertices;
              arg.indexFormat = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INDEX_FORMAT_8BIT;
              arg.baseGeometryIndexAndFlags = masked ? NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION
                                                     : NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_GEOMETRY_FLAG_OPAQUE;
              arg.indexBufferStride = 1;
              arg.geometryIndexAndFlagsBufferStride = 4;
              arg.geometryIndexAndFlagsBuffer = template_geometry_->GetGPUVirtualAddress();
              arg.indexBuffer = set_pool_->GetGPUVirtualAddress() + ((uint64_t{set.offset} + set.clusters) * 16) + (uint64_t{desc_triangle} * 3);
              if (set.micromaps) {
                arg.opacityMicromapIndexFormat = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INDEX_FORMAT_32BIT;
                arg.opacityMicromapIndexBufferStride = 4;
                arg.opacityMicromapArray = set.micromaps->GetGPUVirtualAddress();
                arg.opacityMicromapIndexBuffer = set.micromap_inputs->GetGPUVirtualAddress() + set.micromap_indices + (uint64_t{desc_triangle} * 4);
              }
              template_destinations[template_builds] = templates_->GetGPUVirtualAddress() + (uint64_t{first_template + cluster} * template_bytes_);
              ++template_builds;
            }
          } else if (!templates_dropped) {
            const std::unordered_set<uint64_t> used = used_sets();
            for (auto& [key, other] : sets_) {
              if (other.template_first == UINT32_MAX || used.contains(key)) continue;
              pending_set_frees_.push_back({.value = own_value_ + 1, .template_first = other.template_first, .templates = other.clusters});
              other.template_first = UINT32_MAX;
            }
            templates_dropped = true;
          }
        }
#endif
        if ((set.template_first != UINT32_MAX && set.template_masked == masked) != templated_run) continue;
        const uint64_t clusters = uint64_t{set.clusters} * draw.instances;
        const uint64_t triangles = uint64_t{set.triangles} * draw.instances;
        const uint64_t block_end = uint64_t{draw.first_vertex} + (uint64_t{set.vertices} * draw.instances);
        const uint64_t run_clusters = templated_run ? compact_clusters : compact_clusters - templated_clusters;
        const uint64_t run_room = templated_run ? cluster_room_ - built_limit_ : built_limit_;
        const bool cluster_room = run_clusters + clusters <= run_room;
        clusters_crowded_ = clusters_crowded_ || (run_clusters + clusters) * 4 > run_room * 3;
        (templated_run ? templated_wanted_ : built_wanted_) += clusters;
        if (block_end > TRANSFER_VERTICES || !cluster_room || dynamic_triangles_used + triangles > DYNAMIC_TRIANGLES || (masked && dynamic_uvs_used + (triangles * 3) > dynamic_uvs_)
            || cluster_vertices + (uint64_t{set.vertices} * draw.instances) > TRANSFER_VERTICES) {
          continue;
        }
        cluster_vertices += uint64_t{set.vertices} * draw.instances;
        const std::array<uint32_t, 16> entry = {
            compact_clusters, draw.first_vertex, set.offset * 16, set.clusters,
            set.vertices, set.triangles, STATIC_TRIANGLES + (static_cast<uint32_t>(slot_index) * DYNAMIC_TRIANGLES) + dynamic_triangles_used,
            masked ? STATIC_UVS + (static_cast<uint32_t>(slot_index) * dynamic_uvs_) + dynamic_uvs_used : UINT32_MAX,
            draw.slice | (RecordFlags({.flags = draw.flags | dlaa_bridge::RT_DYNAMIC}, masked, version) << 16), capture_count + compact_drawn,
            draw.flags | own_cutoff_flags,
            templated_run ? set.template_first : 0u,
            ((draw.flags & dlaa_bridge::RT_INSTANCED) != 0 && sway_rows.contains(static_cast<uint32_t>(block_end))) ? static_cast<uint32_t>(block_end)
                                                                                                                  : UINT32_MAX,
            0u, 0u, 0u,
        };
        std::memcpy(slot->compact_data + (uint64_t{compact_drawn} * 64), entry.data(), sizeof(entry));
        if (masked && set_micromap_room != 0 && !set.micromaps_tried && set.triangles <= MICROMAP_TRIANGLES) {
          set.micromaps_tried = true;
          --set_micromap_room;
          set_micromap_reads.push_back({.key = draw.set, .build = set.generation, .triangles = set.triangles, .uv_offset = entry[7],
                                        .slice = draw.slice, .set = true, .grass = (draw.flags & dlaa_bridge::RT_GRASS) != 0,
                                        .cutoff = cutoff_of(draw.flags), .gpu = gpu_bakes});
        }
        compact_keys.push_back(draw.set);
        compact_flags.push_back(draw.flags);
        ++compact_drawn;
        compact_clusters += static_cast<uint32_t>(clusters);
        dynamic_triangles_used += static_cast<uint32_t>(triangles);
        if (masked) dynamic_uvs_used += static_cast<uint32_t>(triangles * 3);
        compact_masked = compact_masked || (masked && !grass_run);
        dynamic_triangles += triangles;
        if (masked) grass_triangles += triangles;
        if ((draw.flags & dlaa_bridge::RT_TERRAIN) != 0) terrain_triangles += triangles;
        vertex_end = std::max(vertex_end, static_cast<uint32_t>(block_end));
      }
      if (templated_run) templated_clusters = compact_clusters;
      if (!templated_run && grass_run) grass_end = compact_clusters;
    }
#if defined(RENODX_NVAPI_CLUSTERS)
    // The new templates, built from the sets' triangles (their places copied where the build reads them).
    if (template_builds != 0) {
      std::memcpy(slot->compact_data + COMPACT_TEMPLATE_COUNT_OFFSET, &template_builds, sizeof(template_builds));
      const auto to_copy = Transition(template_destinations_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
      list->ResourceBarrier(1, &to_copy);
      list->CopyBufferRegion(template_destinations_.Get(), 0, slot->compact.Get(), COMPACT_TEMPLATE_DESTINATIONS_OFFSET, uint64_t{template_builds} * 8);
      const auto copied = Transition(template_destinations_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      list->ResourceBarrier(1, &copied);
      const uint64_t upload = slot->compact->GetGPUVirtualAddress();
      const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC templates_desc = {
          .inputs = template_inputs_,
          .addressResolutionFlags = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_ADDRESS_RESOLUTION_FLAG_NONE,
          .batchResultData = 0,
          .batchScratchData = cluster_scratch_->GetGPUVirtualAddress(),
          .destinationAddressArray = {.StartAddress = template_destinations_->GetGPUVirtualAddress(), .StrideInBytes = 8},
          .resultSizeArray = {},
          .indirectArgArray = {.StartAddress = upload + COMPACT_TEMPLATE_ARGS_OFFSET,
                               .StrideInBytes = sizeof(NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_TEMPLATE_ARGS)},
          .indirectArgCount = upload + COMPACT_TEMPLATE_COUNT_OFFSET,
      };
      const NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS templates_params = {
          .version = NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS_VER, .pDesc = &templates_desc};
      Mark(list, L"templates");
      const NvAPI_Status templates_built = NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation(list, &templates_params);
      const D3D12_RESOURCE_BARRIER built_templates = {.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV};
      list->ResourceBarrier(1, &built_templates);
      // Refused: this request instantiates none (its CLASes would read unwritten templates) and the new sets give theirs
      // back, so a later request builds them again.
      if (templates_built != NVAPI_OK) {
        templates_refused = true;
        say_refused(1, std::format("the templates' build was refused (NvAPI {}; {} templates of {} sets): units and grass not traced this frame.",
                                   static_cast<int>(templates_built), template_builds, new_templated_sets.size()));
        for (ClusterSet* set : new_templated_sets) {
          pending_set_frees_.push_back({.value = own_value_ + 1, .template_first = set->template_first, .templates = set->clusters});
          set->template_first = UINT32_MAX;
        }
      }
    }
#endif
    vertex_end = std::min(vertex_end, TRANSFER_VERTICES);
#if defined(RENODX_RECORDER)
    recorded_vertex_end_ = vertex_end;
#endif
    if (vertex_end != 0) {
      const UINT rows = (vertex_end + dlaa_bridge::RT_TRANSFER_WIDTH - 1) / dlaa_bridge::RT_TRANSFER_WIDTH;
      const D3D12_TEXTURE_COPY_LOCATION destination = {
          .pResource = vertex_buffer_.Get(),
          .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
          .PlacedFootprint = {.Offset = 0,
                              .Footprint = {.Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .Width = dlaa_bridge::RT_TRANSFER_WIDTH, .Height = rows,
                                            .Depth = 1, .RowPitch = dlaa_bridge::RT_TRANSFER_WIDTH * 16}},
      };
      const D3D12_TEXTURE_COPY_LOCATION source = {.pResource = vertices_.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
      const D3D12_BOX box = {.left = 0, .top = 0, .front = 0, .right = dlaa_bridge::RT_TRANSFER_WIDTH, .bottom = rows, .back = 1};
      Mark(list, L"vertices");
      const auto to_copy = Transition(vertices_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
      list->ResourceBarrier(1, &to_copy);
      list->CopyTextureRegion(&destination, 0, 0, 0, &source, &box);
      const std::array<D3D12_RESOURCE_BARRIER, 2> copied = {
          Transition(vertices_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
          Transition(vertex_buffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
      };
      list->ResourceBarrier(static_cast<UINT>(copied.size()), copied.data());
    }
    // The placed meshes built this request (v14): their captures' rows as placements (first_vertex: the rows' texel).
    std::vector<dlaa_bridge::RtPlacement> built_placed;
    if ((capture_count != 0 || compact_drawn != 0) && vertex_end != 0) {
      list->CopyBufferRegion(slot->left_out.Get(), 0, left_out_start_.Get(), 0, LEFT_OUT_BYTES);
      std::vector<D3D12_RESOURCE_BARRIER> to_scene_data = {
          Transition(records_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
          Transition(uvs_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
          Transition(normals_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
          Transition(vertex_buffer_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
          Transition(slot->left_out.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
      };
      if (atlas_) to_scene_data.push_back(Transition(atlas_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
      list->ResourceBarrier(static_cast<UINT>(to_scene_data.size()), to_scene_data.data());

      // Places: static meshes keep theirs (records and ranges from the pools), units take this slot's turn. Leaves
      // without an opacity mask (an older addon, or the atlas full) stay opaque.
      struct Placement {
        uint32_t record = UINT32_MAX;
        uint32_t normal_offset = UINT32_MAX;
        uint32_t uv_offset = UINT32_MAX;
        bool masked = false;
      };
      std::vector<Placement> placements(capture_count);
      uint32_t dynamic_records = 0;
      for (uint32_t index = 0; index < capture_count; ++index) {
        const dlaa_bridge::RtCapture& capture = shared.rt_captures[index];
        auto& placement = placements[index];
        const uint32_t triangles = capture.vertex_count / 3;
        if (triangles == 0 || capture.first_vertex + capture.vertex_count > TRANSFER_VERTICES) continue;
        placement.masked = (capture.flags & dlaa_bridge::RT_FOLIAGE) != 0 && version >= 6 && atlas_;
        // The frame's units in this slot's turn of the records and pools (grass alpha-tested: its UVs at its triangles'
        // place in the slot's UVs, ATN 2026-10-28: blocky grass shadows).
        if ((capture.flags & dlaa_bridge::RT_DYNAMIC) != 0) {
          placement.masked = placement.masked && dynamic_uvs_used + triangles * 3 <= dynamic_uvs_;
          // Smoke without its UVs would be opaque squares among the units: left out.
          if (!placement.masked && (capture.flags & dlaa_bridge::RT_SMOKE) != 0) {
            placement = {};
            continue;
          }
          if (dynamic_triangles_used + triangles > DYNAMIC_TRIANGLES || cluster_vertices + (triangles * 3) > TRANSFER_VERTICES) continue;
          cluster_vertices += triangles * 3;
          placement.record = STATIC_RECORDS + static_cast<uint32_t>(slot_index) * dlaa_bridge::RT_CAPTURES + dynamic_records++;
          placement.normal_offset = STATIC_TRIANGLES + static_cast<uint32_t>(slot_index) * DYNAMIC_TRIANGLES + dynamic_triangles_used;
          if (placement.masked) {
            placement.uv_offset = STATIC_UVS + static_cast<uint32_t>(slot_index) * dynamic_uvs_ + dynamic_uvs_used;
            dynamic_uvs_used += triangles * 3;
          }
          dynamic_triangles_used += triangles;
          continue;
        }
        if (free_records_.empty() || !free_triangles_.Take(triangles, &placement.normal_offset)) {
          placement = {};
          continue;
        }
        if (placement.masked && !free_uvs_.Take(triangles * 3, &placement.uv_offset)) {
          free_triangles_.Give(placement.normal_offset, triangles);
          placement = {};
          continue;
        }
        placement.record = free_records_.back();
        free_records_.pop_back();
      }
      // Each capture's first triangle in the scene data's count (captures start anywhere: placed draws' rows are four
      // texels an instance, so a capture after them is off the vertex texture's threes; read as threes, the grass's UVs
      // came from the wrong vertices, its alpha test changing every frame).
      uint32_t scene_triangles = 0;
      slot->capture_keys.resize(details_ ? capture_count : 0u);
      slot->capture_flags.resize(details_ ? capture_count : 0u);
      for (uint32_t index = 0; index < capture_count; ++index) {
        const dlaa_bridge::RtCapture& capture = shared.rt_captures[index];
        const auto& placement = placements[index];
        const uint32_t slice = (version >= 6) ? shared.rt_capture_slices[index] : 0u;
        const std::array<uint32_t, 8> entry = {capture.first_vertex, capture.vertex_count / 3 * 3, placement.normal_offset, placement.uv_offset,
                                               placement.record, slice | (RecordFlags(capture, placement.masked, version) << 16),
                                               scene_triangles, capture.flags | own_cutoff_flags};
        std::memcpy(slot->table_data + index * 8, entry.data(), sizeof(entry));
        if (placement.record != UINT32_MAX) scene_triangles += capture.vertex_count / 3;
        if (details_) {
          slot->capture_keys[index] = capture.key;
          slot->capture_flags[index] = capture.flags;
        }
      }
      if (details_) {
        slot->capture_keys.insert(slot->capture_keys.end(), compact_keys.begin(), compact_keys.end());
        slot->capture_flags.insert(slot->capture_flags.end(), compact_flags.begin(), compact_flags.end());
      }
      // In rows of 65535 groups (D3D12's most along x: a frame of over 4.2 M triangles went past it).
      // Grass Ray Tracing Range for grass captured as drawn: the view-projection's row 3.
      const std::array<uint32_t, 4> scene_dispatch = {capture_count, scene_triangles, std::bit_cast<uint32_t>(own_cutoff), std::bit_cast<uint32_t>(grass_range)};
      const uint32_t scene_groups = (scene_triangles + 63) / 64;
      Mark(list, L"scene data");
      list->SetPipelineState(scene_data_pipeline_.Get());
      list->SetComputeRoot32BitConstants(ROOT_DISPATCH, 4, scene_dispatch.data(), 0);
      list->SetComputeRoot32BitConstants(ROOT_DISPATCH, 4, shared.rt_view_projection + 12, 4);
      list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_DATA, vertex_buffer_->GetGPUVirtualAddress());
      list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_INDICES, slot->left_out->GetGPUVirtualAddress());
      list->Dispatch(std::min(scene_groups, 65535u), (scene_groups + 65534) / 65535, 1);
      // The compact draws' clusters: their arguments and records, their triangles' normals and UVs (a group a cluster, in
      // rows of 65535).
      if (compact_drawn != 0) {
        const uint64_t vertices_address = vertex_buffer_->GetGPUVirtualAddress();
        const uint64_t sets_address = set_pool_->GetGPUVirtualAddress();
        const std::array<uint32_t, 8> compact_dispatch = {
            compact_drawn, compact_clusters, first_cluster_record, std::bit_cast<uint32_t>(own_cutoff), static_cast<uint32_t>(vertices_address),
            static_cast<uint32_t>(vertices_address >> 32), static_cast<uint32_t>(sets_address), static_cast<uint32_t>(sets_address >> 32),
        };
        const auto to_arguments = Transition(cluster_args_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        list->ResourceBarrier(1, &to_arguments);
        Mark(list, L"compact");
        list->SetPipelineState(compact_pipeline_.Get());
        list->SetComputeRoot32BitConstants(ROOT_DISPATCH, static_cast<UINT>(compact_dispatch.size()), compact_dispatch.data(), 0);
        const uint64_t templates_address = templates_->GetGPUVirtualAddress();
        const std::array<uint32_t, 8> templates_dispatch = {
            static_cast<uint32_t>(templates_address), static_cast<uint32_t>(templates_address >> 32), template_bytes_, templated_clusters,
            (cluster_room_ - built_limit_) * 32, sway_flags | (units_range << 16), std::bit_cast<uint32_t>(shared.rt_sway_range), std::bit_cast<uint32_t>(grass_range),
        };
        list->SetComputeRoot32BitConstants(ROOT_BOUNCE, static_cast<UINT>(templates_dispatch.size()), templates_dispatch.data(), 0);
        // The sway's view: the view-projection's rows 0, 1 and 3.
        list->SetComputeRoot32BitConstants(ROOT_BOUNCE, 8, shared.rt_view_projection, 8);
        list->SetComputeRoot32BitConstants(ROOT_BOUNCE, 4, shared.rt_view_projection + 12, 16);
        list->SetComputeRootShaderResourceView(ROOT_SCENE, sets_address);
        list->SetComputeRootShaderResourceView(ROOT_CAPTURE_TABLE, slot->compact->GetGPUVirtualAddress());
        list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_DESCS, cluster_args_->GetGPUVirtualAddress());
        list->Dispatch(std::min(compact_clusters, 65535u), (compact_clusters + 65534) / 65535, 1);
        const auto arguments_written = Transition(cluster_args_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        list->ResourceBarrier(1, &arguments_written);
        list->SetComputeRootShaderResourceView(ROOT_CAPTURE_TABLE, slot->table->GetGPUVirtualAddress());
        list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_DESCS, tlas_scratch_->GetGPUVirtualAddress());
      }
      const std::array<D3D12_RESOURCE_BARRIER, 5> scene_written = {
          Transition(records_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
          Transition(uvs_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
          Transition(normals_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
          Transition(vertex_buffer_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
          Transition(slot->left_out.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
      };
      list->ResourceBarrier(static_cast<UINT>(scene_written.size()), scene_written.data());
      if (details_) {
        list->CopyBufferRegion(slot->left_out_readback.Get(), 0, slot->left_out.Get(), 0, LEFT_OUT_BYTES);
      }
      const auto left_out_back = Transition(slot->left_out.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
      list->ResourceBarrier(1, &left_out_back);
      for (const Root root : {ROOT_MICROMAP_DATA, ROOT_MICROMAP_INDICES}) {
        list->SetComputeRootUnorderedAccessView(root, tlas_scratch_->GetGPUVirtualAddress());
      }

      // Opacity micromaps (DXR 1.2) baked for leaves built two rounds of the slots back or more (their first compaction and
      // rows read: that slot's next trace, the third after with two requests a frame): their arrays built now from the bakes'
      // upload buffers (none when every triangle has a special index), their BLASes again below.
      struct MicromapArray {
        std::unique_ptr<MicromapBake> bake;
        ComPtr<ID3D12Resource> array;
        ComPtr<ID3D12Resource> inputs;  // upload: the data, descs, indices (read by the builds alone)
        uint64_t indices_offset = 0;
      };
      std::vector<MicromapArray> micromap_arrays;
      if (dxr12_ && !micromaps_baked_.empty()) {
        std::vector<D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_DESC> array_descs(micromaps_baked_.size());
        std::vector<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC> array_builds;
        std::vector<std::unique_ptr<MicromapBake>> set_bakes;  // their histograms read by the builds below
        uint64_t array_scratch = 0;
        for (std::unique_ptr<MicromapBake>& baked : micromaps_baked_) {
          ClusterSet* set = nullptr;
          if (baked->set) {
            const auto found = sets_.find(baked->key);
            if (found == sets_.end() || found->second.generation != baked->build) {
              baked.reset();
              continue;
            }
            set = &found->second;
          } else if (const auto mesh = meshes_.find(baked->key); mesh == meshes_.end() || mesh->second.build != baked->build) {
            releases_.push_back({.value = own_value_ + 1, .resource = std::move(baked->positions)});
            baked.reset();
            continue;
          }
          if (own_value_ + 1 < baked->value + (2 * SLOTS)) continue;
          // Its data, descs and indices: a GPU bake's where the bake left them (its request's shared outputs, kept until
          // this request's builds are past), a CPU bake's uploaded.
          ComPtr<ID3D12Resource> inputs;
          uint64_t indices_offset = 0;
          D3D12_GPU_VIRTUAL_ADDRESS data_address = 0;
          D3D12_GPU_VIRTUAL_ADDRESS descs_address = 0;
          if (baked->gpu) {
            inputs = baked->gpu_indices;
            indices_offset = baked->indices_offset;
            data_address = baked->gpu_arrays->GetGPUVirtualAddress() + baked->arrays_offset;
            descs_address = baked->gpu_descs->GetGPUVirtualAddress() + baked->descs_offset;
            releases_.push_back({.value = own_value_ + 1, .resource = baked->gpu_arrays});
            releases_.push_back({.value = own_value_ + 1, .resource = baked->gpu_descs});
          } else {
            const uint64_t descs_offset = Align(baked->data.size(), 256);
            indices_offset = Align(descs_offset + (baked->descs.size() * sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_DESC)), 256);
            inputs = Buffer(indices_offset + (baked->indices.size() * 4), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                            D3D12_RESOURCE_STATE_GENERIC_READ);
            uint8_t* mapped = nullptr;
            if (!inputs || FAILED(inputs->Map(0, nullptr, reinterpret_cast<void**>(&mapped)))) continue;
            std::memcpy(mapped, baked->data.data(), baked->data.size());
            std::memcpy(mapped + descs_offset, baked->descs.data(), baked->descs.size() * sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_DESC));
            std::memcpy(mapped + indices_offset, baked->indices.data(), baked->indices.size() * 4);
            inputs->Unmap(0, nullptr);
            data_address = inputs->GetGPUVirtualAddress();
            descs_address = data_address + descs_offset;
          }
          ComPtr<ID3D12Resource> array;
          if (!baked->histogram.empty()) {
            D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_DESC& array_desc = array_descs[array_builds.size()];
            array_desc = {
                .NumOmmHistogramEntries = static_cast<UINT>(baked->histogram.size()),
                .pOmmHistogram = baked->histogram.data(),
                .InputBuffer = data_address,
                .PerOmmDescs = {.StartAddress = descs_address, .StrideInBytes = sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_DESC)},
            };
            const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS array_inputs = {
                .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_OPACITY_MICROMAP_ARRAY,
                .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
                .NumDescs = 1,
                .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
                .pOpacityMicromapArrayDesc = &array_desc,
            };
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
            device_->GetRaytracingAccelerationStructurePrebuildInfo(&array_inputs, &info);
            array = Buffer(info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                           D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE,
                           D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
            if (!array) continue;
            array_builds.push_back({.DestAccelerationStructureData = array->GetGPUVirtualAddress(), .Inputs = array_inputs,
                                    .ScratchAccelerationStructureData = array_scratch});
            array_scratch = Align(array_scratch + info.ScratchDataSizeInBytes, 256);
          }
          if (set != nullptr) {
            // Its templates again, with the micromaps (the old ones given back once the GPU is past this request); a set
            // whose triangles are all wholly opaque or clear keeps its templates (no array).
            if (array) {
              pending_set_frees_.push_back({.value = own_value_ + 1, .template_first = set->template_first,
                                            .templates = (set->template_first != UINT32_MAX) ? set->clusters : 0u});
              set->template_first = UINT32_MAX;
              // Past this request's followers too: the Light Bounce's and reflections' requests trace its scene.
              for (auto* kept : {std::addressof(set->micromaps), std::addressof(set->micromap_inputs)}) {
                if (*kept) releases_.push_back({.value = own_value_ + 1 + SLOTS, .resource = std::move(*kept)});
              }
              set->micromaps = std::move(array);
              set->micromap_inputs = std::move(inputs);
              set->micromap_indices = indices_offset;
              ++micromap_tally_[3];
#if defined(RENODX_RECORDER)
              if (journal_on_) {
                set_journal_[baked->key] = {.build = set->generation, .unread = baked->gpu && baked->indices.empty(), .data = baked->data, .descs = baked->descs,
                                            .indices = baked->indices, .histogram = baked->histogram};
              }
#endif
            }
            set_bakes.push_back(std::move(baked));
            continue;
          }
          micromap_arrays.push_back({.bake = std::move(baked), .array = std::move(array), .inputs = std::move(inputs), .indices_offset = indices_offset});
        }
        std::erase(micromaps_baked_, nullptr);
        if (!array_builds.empty() && ensure_scratch(array_scratch)) {
          Mark(list, L"micromaps");
          for (auto& build : array_builds) {
            build.ScratchAccelerationStructureData += scratch_->GetGPUVirtualAddress();
            list->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
          }
          list->ResourceBarrier(1, &uav_barrier);
        } else if (!array_builds.empty()) {
          std::erase_if(micromap_arrays, [](const MicromapArray& made) { return made.array.Get() != nullptr; });
        }
      }

      // The BLASes, built together: static ones once (for tracing, then compacted), the units' one refitted to this
      // request's poses while its draws are the same (rebuilt every UNITS_REBUILD_FRAMES or when they change).
      std::vector<D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC> triangle_descs(capture_count);
      std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries(capture_count);
      std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> unit_geometries;
      std::vector<uint32_t> unit_layout;  // their vertices (the top bit: alpha-tested)
      // Each unit draw's first vertex, triangles, normals, record flags, UVs, atlas slice and flags (Mega Geometry's clusters).
      std::vector<std::array<uint32_t, 7>> unit_sources;
      uint32_t units_record = UINT32_MAX;
      bool units_masked = false;  // grass among them (not forced opaque)
      struct Build {
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
        bool compact = false;
      };
      std::vector<Build> builds;
      uint64_t scratch_needed = 0;
      // A static BLAS (built for tracing, then compacted) of `geometry` among the builds: its buffer (null: no room) and
      // size.
      const auto build_static = [&](const D3D12_RAYTRACING_GEOMETRY_DESC* geometry, uint64_t* bytes) {
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
            .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
            .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE
                     | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION,
            .NumDescs = 1,
            .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
            .pGeometryDescs = geometry,
        };
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
        device_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
        BlasPlace blas = PlaceBlas(info.ResultDataMaxSizeInBytes, BUILD_POOL);
        if (blas.address != 0) {
          builds.push_back({.desc = {.DestAccelerationStructureData = blas.address, .Inputs = inputs,
                                     .ScratchAccelerationStructureData = scratch_needed},
                            .compact = true});
          scratch_needed = Align(scratch_needed + info.ScratchDataSizeInBytes, 256);
          *bytes = info.ResultDataMaxSizeInBytes;
        }
        return blas;
      };
      // New leaves whose micromaps are baked (with an addon's RT_MICROMAPS, while there is room): their positions, UVs and
      // mask slice copied after the builds.
      std::vector<MicromapRead> micromap_reads = std::move(set_micromap_reads);
      size_t micromap_room = (bake_micromaps_ && dxr12_ && (shared.rt_flags & dlaa_bridge::RT_MICROMAPS) != 0)
                                 ? MICROMAP_BAKES - std::min(MICROMAP_BAKES, micromap_reads_.size() + micromap_queue_.size()
                                                                                 + MicromapsBaking() + micromaps_baked_.size())
                                 : 0u;
#if defined(RENODX_RECORDER)
      if (SnapshotWaiting()) micromap_room = 0;
#endif
      // Instanced captures' rows to read back (their first texel in the vertex buffer).
      std::vector<Slot::RowRead> row_copies;
      std::vector<uint32_t> row_sources;
      uint32_t rows_used = 0;
#if defined(RENODX_RECORDER)
      std::vector<Slot::JournalRead> journal_copies;  // the journal's: the new static meshes (`offset`: their first vertex here)
#endif
      for (uint32_t index = 0; index < capture_count; ++index) {
        const dlaa_bridge::RtCapture& capture = shared.rt_captures[index];
        const auto& placement = placements[index];
        if (placement.record == UINT32_MAX) continue;
        triangle_descs[index] = {
            .IndexFormat = DXGI_FORMAT_UNKNOWN,
            .VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
            .VertexCount = capture.vertex_count / 3 * 3,
            .VertexBuffer = {.StartAddress = vertex_buffer_->GetGPUVirtualAddress() + uint64_t{capture.first_vertex} * 16, .StrideInBytes = 16},
        };
        geometries[index] = {
            .Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES,
            .Flags = placement.masked ? D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION : D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE,
            .Triangles = triangle_descs[index],
        };
        if ((capture.flags & dlaa_bridge::RT_DYNAMIC) != 0) {
          if (units_record == UINT32_MAX) units_record = placement.record;
          unit_geometries.push_back(geometries[index]);
          unit_layout.push_back((capture.vertex_count / 3 * 3) | (placement.masked ? 0x80000000u : 0u));
          unit_sources.push_back({capture.first_vertex, capture.vertex_count / 3, placement.normal_offset, RecordFlags(capture, placement.masked, version),
                                  placement.uv_offset, (version >= 6) ? shared.rt_capture_slices[index] : 0u, capture.flags});
          units_masked = units_masked || placement.masked;
          dynamic_triangles += capture.vertex_count / 3;
          if (placement.masked) grass_triangles += capture.vertex_count / 3;
          if ((capture.flags & dlaa_bridge::RT_TERRAIN) != 0) terrain_triangles += capture.vertex_count / 3;
          continue;
        }
        uint64_t bytes = 0;
        BlasPlace blas = build_static(&geometries[index], &bytes);
        if (blas.address == 0) {
          // No room: its place goes back now (nothing references it).
          free_records_.push_back(placement.record);
          free_triangles_.Give(placement.normal_offset, capture.vertex_count / 3);
          if (placement.uv_offset != UINT32_MAX) free_uvs_.Give(placement.uv_offset, capture.vertex_count / 3 * 3);
          continue;
        }
        Mesh& mesh = meshes_[capture.key];
        if (mesh.blas.address != 0) ReleaseMesh(&mesh);
        mesh = {.blas = std::move(blas), .build = ++builds_, .bytes = bytes, .drawn_frame = frame,
                .triangles = capture.vertex_count / 3, .record = placement.record, .normal_offset = placement.normal_offset,
                .uv_offset = placement.uv_offset, .terrain = (capture.flags & dlaa_bridge::RT_TERRAIN) != 0, .foliage = placement.masked,
                .grass = placement.masked && (capture.flags & dlaa_bridge::RT_GRASS) != 0,
                .solid_cut_out = placement.masked && (capture.flags & (dlaa_bridge::RT_GRASS | dlaa_bridge::RT_SOLID_CUT_OUT)) == dlaa_bridge::RT_SOLID_CUT_OUT};
        if (version >= 14 && (capture.flags & dlaa_bridge::RT_INSTANCED) != 0) {
          mesh.placed = true;
          built_placed.push_back({.key = capture.key, .first_vertex = capture.first_vertex + capture.vertex_count, .instances = capture.instances});
        } else if (version >= 10 && (capture.flags & dlaa_bridge::RT_INSTANCED) != 0 && capture.instances > 1
                   && rows_used + capture.instances <= ROWS_READBACK) {
          row_copies.push_back({.key = capture.key, .build = mesh.build, .offset = rows_used, .count = capture.instances});
          row_sources.push_back(capture.first_vertex + capture.vertex_count);
          rows_used += capture.instances;
        }
        if (placement.masked && micromap_room != 0 && mesh.triangles <= MICROMAP_TRIANGLES) {
          micromap_reads.push_back({.key = capture.key, .build = mesh.build, .first_vertex = capture.first_vertex, .triangles = mesh.triangles,
                                    .uv_offset = placement.uv_offset, .slice = shared.rt_capture_slices[index], .grass = mesh.grass,
                                    .cutoff = cutoff_of(capture.flags), .gpu = gpu_bakes});
          --micromap_room;
        }
        slot->compactions.emplace_back(capture.key, mesh.build);
#if defined(RENODX_RECORDER)
        if (journal_on_) journal_copies.push_back({.key = capture.key, .build = mesh.build, .offset = capture.first_vertex, .vertices = mesh.triangles * 3});
#endif
        ++built;
      }
      // The leaves whose micromaps were baked: their BLAS built again with them from their kept positions (a new build,
      // compacted in turn); the old one released once the GPU is past this request.
      std::vector<D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC> micromap_triangles(micromap_arrays.size());
      std::vector<D3D12_RAYTRACING_GEOMETRY_OMM_LINKAGE_DESC> micromap_linkages(micromap_arrays.size());
      std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> micromap_geometries(micromap_arrays.size());
      for (size_t index = 0; index < micromap_arrays.size(); ++index) {
        MicromapArray& made = micromap_arrays[index];
        const auto found = meshes_.find(made.bake->key);
        if (found == meshes_.end() || found->second.build != made.bake->build) continue;
        Mesh& mesh = found->second;
        micromap_triangles[index] = {
            .IndexFormat = DXGI_FORMAT_UNKNOWN,
            .VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
            .VertexCount = mesh.triangles * 3,
            .VertexBuffer = {.StartAddress = made.bake->positions->GetGPUVirtualAddress() + made.bake->positions_offset, .StrideInBytes = 16},
        };
        micromap_linkages[index] = {
            .OpacityMicromapIndexBuffer = {.StartAddress = made.inputs->GetGPUVirtualAddress() + made.indices_offset, .StrideInBytes = 4},
            .OpacityMicromapIndexFormat = DXGI_FORMAT_R32_UINT,
            .OpacityMicromapBaseLocation = 0,
            .OpacityMicromapArray = made.array ? made.array->GetGPUVirtualAddress() : 0,
        };
        micromap_geometries[index] = {
            .Type = D3D12_RAYTRACING_GEOMETRY_TYPE_OMM_TRIANGLES,
            .Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION,
            .OmmTriangles = {.pTriangles = &micromap_triangles[index], .pOmmLinkage = &micromap_linkages[index]},
        };
        uint64_t bytes = 0;
        BlasPlace blas = build_static(&micromap_geometries[index], &bytes);
        if (blas.address == 0) continue;
        if (details_) {
          slot->micromap_builds += std::format("; micromap BLAS {} triangles, {} KB, {} levels, {}", mesh.triangles,
                                               made.array ? (made.array->GetDesc().Width >> 10) : 0u, made.bake->histogram.size(),
                                               made.bake->gpu ? "GPU bake" : "CPU bake");
        }
        ReleaseBlas(&mesh.blas);
        mesh.blas = std::move(blas);
        mesh.bytes = bytes;
        mesh.build = ++builds_;
        mesh.micromaps = std::move(made.array);
        slot->compactions.emplace_back(made.bake->key, mesh.build);
#if defined(RENODX_RECORDER)
        if (journal_on_) {
          micromap_journal_[made.bake->key] = {.build = mesh.build, .unread = made.bake->gpu && made.bake->indices.empty(), .data = made.bake->data, .descs = made.bake->descs,
                                               .indices = made.bake->indices, .histogram = made.bake->histogram};
        }
#endif
        ++micromaps_built;
      }
      // What they did not keep (their positions and inputs; the arrays of leaves gone or without room) goes once the GPU
      // is past this request.
      for (MicromapArray& made : micromap_arrays) {
        for (ComPtr<ID3D12Resource>* resource : {std::addressof(made.array), std::addressof(made.inputs), std::addressof(made.bake->positions)}) {
          if (*resource) releases_.push_back({.value = own_value_ + 1, .resource = std::move(*resource)});
        }
      }
      // The smoke (the addon's last capture: rt_smoke's quads) in a BLAS of its own, its instance met only by the rays
      // toward the sun: the ambient's, the local lights' and the paths' rays went through its particles, an alpha test each
      // and their traversal (at 4K through 1000 of them 30-40 ms a frame each). Sprite soldiers and far trees (RT_SUN_ONLY)
      // right before it with it. Smoke before other dynamic captures (not their tail) stays in the units'. Never Mega
      // Geometry's clusters: made from the particles in their list's order, each spanned the battlefield and every ray
      // went into hundreds of them (the trace 3.6 -> 194 ms as an arrow volley's smoke came, ATN's 2026-11-01 stress
      // battle); the BLAS build sorts them by place.
      const auto sun_alone = [](const std::array<uint32_t, 7>& source) {
        return (source[3] & RECORD_SMOKE) != 0 || (source[6] & dlaa_bridge::RT_SUN_ONLY) != 0;
      };
      size_t unit_count = unit_sources.size();
      while (unit_count != 0 && sun_alone(unit_sources[unit_count - 1])) {
        --unit_count;
      }
      const size_t smoke_count = unit_sources.size() - unit_count;
      // The smoke's records follow the units' captures' (one each, in capture order).
      smoke_record = units_record + static_cast<uint32_t>(unit_count);
      unit_layout.resize(unit_count);
      // The units: refitted in place while the request's unit draws are those of the last build (without Mega Geometry).
      if (unit_count != 0 && !clusters_) {
        const bool refit = units_blas_ && unit_layout == units_layout_ && units_refits_ < UNITS_REBUILD_FRAMES;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
            .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
            .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD
                     | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE,
            .NumDescs = static_cast<UINT>(unit_count),
            .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
            .pGeometryDescs = unit_geometries.data(),
        };
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
        device_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
        if (!refit && info.ResultDataMaxSizeInBytes > units_bytes_) {
          if (units_blas_) releases_.push_back({.value = own_value_ + 1, .resource = std::move(units_blas_)});
          units_bytes_ = info.ResultDataMaxSizeInBytes + info.ResultDataMaxSizeInBytes / 4;
          units_blas_ = Buffer(units_bytes_, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
          if (!units_blas_) units_bytes_ = 0;
        }
        if (units_blas_) {
          if (refit) inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
          builds.push_back({.desc = {.DestAccelerationStructureData = units_blas_->GetGPUVirtualAddress(), .Inputs = inputs,
                                     .SourceAccelerationStructureData = refit ? units_blas_->GetGPUVirtualAddress() : 0,
                                     .ScratchAccelerationStructureData = scratch_needed}});
          scratch_needed = Align(scratch_needed + (refit ? info.UpdateScratchDataSizeInBytes : info.ScratchDataSizeInBytes), 256);
          units_refits_ = refit ? units_refits_ + 1 : 0;
          units_layout_ = std::move(unit_layout);
          units_built = true;
          units_refitted = refit;
        }
      }
      // The smoke: built anew each request (its particles change), for its rays' traversal; Smoke BLAS Fast Build
      // (RT_SCENE_SMOKE_FAST_BUILD, or RENODX_TW_SMOKE_FAST_BUILD for the harness) for a fast build (262,144 particles at
      // 4K: 1.69 -> 1.03 ms), the rays meeting its particles in another order.
      if (smoke_count != 0) {
        const bool fast_build = (shared.rt_scene_flags & dlaa_bridge::RT_SCENE_SMOKE_FAST_BUILD) != 0
                                || GetEnvironmentVariableW(L"RENODX_TW_SMOKE_FAST_BUILD", nullptr, 0) != 0;
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
            .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
            .Flags = (fast_build ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD
                                 : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE),
            .NumDescs = static_cast<UINT>(smoke_count),
            .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
            .pGeometryDescs = unit_geometries.data() + unit_count,
        };
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
        device_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
        if (info.ResultDataMaxSizeInBytes > smoke_bytes_) {
          if (smoke_blas_) {
            releases_.push_back({.value = own_value_ + 1, .resource = std::move(smoke_blas_)});
          }
          smoke_bytes_ = info.ResultDataMaxSizeInBytes + (info.ResultDataMaxSizeInBytes / 4);
          smoke_blas_ = Buffer(smoke_bytes_, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
          if (!smoke_blas_) {
            smoke_bytes_ = 0;
          }
        }
        if (smoke_blas_) {
          builds.push_back({.desc = {.DestAccelerationStructureData = smoke_blas_->GetGPUVirtualAddress(), .Inputs = inputs,
                                     .ScratchAccelerationStructureData = scratch_needed}});
          scratch_needed = Align(scratch_needed + info.ScratchDataSizeInBytes, 256);
          smoke_built = true;
        }
      }
      if (!ensure_scratch(scratch_needed)) return skip("Ray tracing: the BLAS scratch allocation failed");
      Mark(list, L"BLAS");
      size_t compact_slot = 0;
      for (auto& build : builds) {
        build.desc.ScratchAccelerationStructureData += scratch_->GetGPUVirtualAddress();
        const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC postbuild = {
            .DestBuffer = build.compact ? slot->sizes->GetGPUVirtualAddress() + (compact_slot++) * sizeof(uint64_t) : 0,
            .InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE,
        };
        list->BuildRaytracingAccelerationStructure(&build.desc, build.compact ? 1 : 0, build.compact ? &postbuild : nullptr);
      }
#if defined(RENODX_NVAPI_CLUSTERS)
      // Mega Geometry: the units' captures split into clusters (CLAS) after the compact draws' (the compact pass wrote
      // theirs), built with one cluster BLAS from them all; each cluster a geometry whose record points at its normals.
      if (clusters_ && (!unit_sources.empty() || compact_clusters != 0)) {
        uint8_t* data = slot->cluster_data;
        auto* args = reinterpret_cast<NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_CLUSTER_ARGS*>(data + CLUSTER_ARGS_OFFSET);
        auto* cluster_records = reinterpret_cast<std::array<uint32_t, 4>*>(data + CLUSTER_RECORDS_OFFSET);
        uint32_t cluster_count = compact_clusters;
        for (size_t source = 0; source < unit_count; ++source) {
          const auto& [first_vertex, unit_triangles, normal_offset, record_flags, uv_offset, slice, capture_flags] = unit_sources[source];
          const bool masked = (record_flags & RECORD_MASKED) != 0;
          const uint32_t cluster_triangles = (((capture_flags & dlaa_bridge::RT_SCATTERED) != 0) ? 2u : CLUSTER_TRIANGLES);
          built_wanted_ += (unit_triangles + cluster_triangles - 1) / cluster_triangles;
          for (uint32_t first = 0; first < unit_triangles && cluster_count - templated_clusters < built_limit_; first += cluster_triangles) {
            const uint32_t count = std::min(cluster_triangles, unit_triangles - first);
            auto& arg = args[cluster_count - templated_clusters];
            arg = {};
            arg.clusterId = cluster_count;
            arg.triangleCount = count;
            arg.vertexCount = count * 3;
            arg.indexFormat = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INDEX_FORMAT_8BIT;
            arg.baseGeometryIndexAndFlags = cluster_count
                                            | (masked ? NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION
                                                      : NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_GEOMETRY_FLAG_OPAQUE);
            arg.indexBufferStride = 1;
            arg.vertexBufferStride = 16;
            arg.indexBuffer = cluster_indices_->GetGPUVirtualAddress();
            arg.vertexBuffer = vertex_buffer_->GetGPUVirtualAddress() + (uint64_t{first_vertex} + uint64_t{first} * 3) * 16;
            cluster_records[cluster_count] = {(masked ? uv_offset + first * 3 : UINT32_MAX), normal_offset + first, slice | (record_flags << 16),
                                              (masked ? std::bit_cast<uint32_t>(cutoff_of(capture_flags)) : 0u)};
            ++cluster_count;
          }
        }
        const uint32_t units_end = cluster_count;  // the smoke's run stays empty (its own BLAS)
        if ((cluster_count - templated_clusters) * 4 >= built_limit_ * 3) {
          clusters_crowded_ = true;
        }
        // The cluster BLASes' runs of the CLASes' addresses (CLUSTER_BLASES: the rest's before and after the grass's, the
        // grass's, the smoke's; without grass the rest's one).
        if (grass_first == grass_end) {
          grass_first = units_end;
          grass_end = units_end;
        }
        const std::array<std::pair<uint32_t, uint32_t>, CLUSTER_BLASES> blas_runs = {
            {{0u, grass_first}, {grass_first, grass_end}, {grass_end, units_end}, {units_end, cluster_count}}};
        // With the sway (rt_sway) the counts start at 0 in sway_counts_ and the lists are sway_lists_'s: an argument for each
        // cluster BLAS in its place (rt_sway counts each run's there). Without it the non-empty runs' only, packed, their
        // count after the destinations (no zero-cluster argument in the batched build).
        const bool sway = sway_flags != 0;
        std::array<uint64_t, CLUSTER_BLASES> destinations = {};
        std::array<size_t, CLUSTER_BLASES> arg_blases = {};  // each argument's cluster BLAS
        uint32_t blas_arg_count = 0;
        for (size_t blas = 0; blas < CLUSTER_BLASES; ++blas) {
          const uint32_t run_clusters = blas_runs[blas].second - blas_runs[blas].first;
          if (!sway && run_clusters == 0) continue;
          const NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_CLUSTER_ARGS blas_args = {
              .clusterCount = sway ? 0u : run_clusters, .reserved = 0,
              .clusterVAs = (sway ? sway_lists_ : clas_addresses_)->GetGPUVirtualAddress() + (uint64_t{blas_runs[blas].first} * 8)};
          std::memcpy(data + CLUSTER_BLAS_ARGS_OFFSET + (uint64_t{blas_arg_count} * 16), &blas_args, sizeof(blas_args));
          destinations[blas_arg_count] = cluster_blases_[blas]->GetGPUVirtualAddress();
          arg_blases[blas_arg_count] = blas;
          ++blas_arg_count;
        }
        const std::array<uint32_t, 2> clas_counts = {sway ? 0u : cluster_count - templated_clusters, sway ? 0u : templated_clusters};
        std::memcpy(data + CLUSTER_COUNT_OFFSET, clas_counts.data(), sizeof(clas_counts));
        std::memcpy(data + CLUSTER_DESTINATION_OFFSET, destinations.data(), sizeof(destinations));
        std::memcpy(data + CLUSTER_BLAS_COUNT_OFFSET, &blas_arg_count, sizeof(blas_arg_count));
        // The captures' clusters' arguments and records, the BLAS's destination (and once the indices) copied where the
        // builds read them.
        const uint64_t captured = cluster_count - compact_clusters;
        std::vector<D3D12_RESOURCE_BARRIER> to_copy = {
            Transition(records_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
            Transition(cluster_destination_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST),
            Transition(cluster_args_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
        };
        list->ResourceBarrier(static_cast<UINT>(to_copy.size()), to_copy.data());
        if (captured != 0) {
          list->CopyBufferRegion(records_.Get(), (uint64_t{first_cluster_record} + compact_clusters) * 16, slot->clusters.Get(),
                                 CLUSTER_RECORDS_OFFSET + (uint64_t{compact_clusters} * 16), captured * 16);
          const uint64_t built_compact = compact_clusters - templated_clusters;
          list->CopyBufferRegion(cluster_args_.Get(), (uint64_t{cluster_room_ - built_limit_} * 32) + (built_compact * 64), slot->clusters.Get(),
                                 CLUSTER_ARGS_OFFSET + (built_compact * 64), captured * 64);
        }
        list->CopyBufferRegion(cluster_destination_.Get(), 0, slot->clusters.Get(), CLUSTER_DESTINATION_OFFSET, CLUSTER_BLASES * 8);
        if (!cluster_indices_written_) list->CopyBufferRegion(cluster_indices_.Get(), 0, slot->clusters.Get(), CLUSTER_INDICES_OFFSET, 256);
        for (auto& barrier : to_copy) std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        if (!cluster_indices_written_) {
          to_copy.push_back(Transition(cluster_indices_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
          cluster_indices_written_ = true;
        }
        list->ResourceBarrier(static_cast<UINT>(to_copy.size()), to_copy.data());
        const uint64_t upload = slot->clusters->GetGPUVirtualAddress();
        const uint32_t templated_room = cluster_room_ - built_limit_;
        const auto sway_pass = [&](uint32_t mode, ID3D12Resource* read, ID3D12Resource* written) {
          const std::array<uint32_t, 8> sway_dispatch = {mode, cluster_count, templated_clusters, templated_room * 32, grass_first, grass_end, units_end, 0u};
          list->SetComputeRootSignature(root_signature_.Get());
          list->SetPipelineState(sway_pipeline_.Get());
          list->SetComputeRoot32BitConstants(ROOT_DISPATCH, static_cast<UINT>(sway_dispatch.size()), sway_dispatch.data(), 0);
          list->SetComputeRootShaderResourceView(ROOT_CAPTURE_TABLE, read->GetGPUVirtualAddress());
          list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_DATA, written->GetGPUVirtualAddress());
          list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_INDICES, sway_counts_->GetGPUVirtualAddress());
          list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_DESCS, clas_addresses_->GetGPUVirtualAddress());
          list->Dispatch(mode == 3 ? 1 : (cluster_count + 63) / 64, 1, 1);
        };
        // The sway's kept clusters: their arguments packed into sway_arguments_ and counted (its counts from 0, the cluster
        // BLASes' list addresses: the upload's).
        if (sway) {
          Mark(list, L"sway");
          std::array<D3D12_RESOURCE_BARRIER, 2> to_pack = {
              Transition(sway_counts_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
              Transition(sway_arguments_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};
          list->ResourceBarrier(static_cast<UINT>(to_pack.size()), to_pack.data());
          list->CopyBufferRegion(sway_counts_.Get(), 0, slot->clusters.Get(), CLUSTER_BLAS_ARGS_OFFSET, (CLUSTER_BLASES * 16) + 8);
          const auto counting = Transition(sway_counts_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
          list->ResourceBarrier(1, &counting);
          // Which are kept, the kept before each group, then their arguments (rt_sway's MODE_MASKS, MODE_FIRSTS, MODE_KEPT).
          for (const uint32_t mode : {2u, 3u, 0u}) {
            sway_pass(mode, cluster_args_.Get(), sway_arguments_.Get());
            list->ResourceBarrier(1, &uav_barrier);
          }
          to_pack = {Transition(sway_counts_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                     Transition(sway_arguments_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)};
          list->ResourceBarrier(static_cast<UINT>(to_pack.size()), to_pack.data());
        }
        // The instantiated CLASes into the first batch (their addresses first), those built from triangles after (with the
        // sway: the kept ones, by its counts, the built ones' addresses after the instantiated ones' room).
        const uint64_t arguments = (sway ? sway_arguments_ : cluster_args_)->GetGPUVirtualAddress();
        const uint64_t counts = sway ? sway_counts_->GetGPUVirtualAddress() + (CLUSTER_BLASES * 16) : upload + CLUSTER_COUNT_OFFSET;
        const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC instantiate_desc = {
            .inputs = instantiate_inputs_,
            .addressResolutionFlags = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_ADDRESS_RESOLUTION_FLAG_NONE,
            .batchResultData = clas_->GetGPUVirtualAddress(),
            .batchScratchData = cluster_scratch_->GetGPUVirtualAddress(),
            .destinationAddressArray = {.StartAddress = clas_addresses_->GetGPUVirtualAddress(), .StrideInBytes = 8},
            .resultSizeArray = {},
            .indirectArgArray = {.StartAddress = arguments,
                                 .StrideInBytes = sizeof(NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_INSTANTIATE_TEMPLATE_ARGS)},
            .indirectArgCount = counts + 4,
        };
        const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC clas_desc = {
            .inputs = clas_inputs_,
            .addressResolutionFlags = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_ADDRESS_RESOLUTION_FLAG_NONE,
            .batchResultData = clas_->GetGPUVirtualAddress() + instantiated_bytes_,
            .batchScratchData = cluster_scratch_->GetGPUVirtualAddress(),
            .destinationAddressArray = {.StartAddress = clas_addresses_->GetGPUVirtualAddress() + (uint64_t{sway ? templated_room : templated_clusters} * 8),
                                        .StrideInBytes = 8},
            .resultSizeArray = {},
            .indirectArgArray = {.StartAddress = arguments + (uint64_t{templated_room} * 32),
                                 .StrideInBytes = sizeof(NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_CLUSTER_ARGS)},
            .indirectArgCount = counts,
        };
        const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC blas_desc = {
            .inputs = cluster_blas_inputs_,
            .addressResolutionFlags = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_ADDRESS_RESOLUTION_FLAG_NONE,
            .batchResultData = 0,
            .batchScratchData = cluster_scratch_->GetGPUVirtualAddress(),
            .destinationAddressArray = {.StartAddress = cluster_destination_->GetGPUVirtualAddress(), .StrideInBytes = 8},
            .resultSizeArray = {},
            .indirectArgArray = {.StartAddress = sway ? sway_counts_->GetGPUVirtualAddress() : upload + CLUSTER_BLAS_ARGS_OFFSET,
                                 .StrideInBytes = sizeof(NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_CLUSTER_ARGS)},
            .indirectArgCount = upload + CLUSTER_BLAS_COUNT_OFFSET,
        };
        const NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS instantiate_params = {
            .version = NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS_VER, .pDesc = &instantiate_desc};
        const NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS clas_params = {
            .version = NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS_VER, .pDesc = &clas_desc};
        // Their build timed apart (a trace's slot leaves the paths' two stamps free).
        Mark(list, L"clusters");
        Stamp(list, slot_index * STAMPS + 4);
        bool clas_built = !templates_refused;
        if (clas_built && templated_clusters != 0) {
          const NvAPI_Status instantiated = NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation(list, &instantiate_params);
          list->ResourceBarrier(1, &uav_barrier);
          clas_built = (instantiated == NVAPI_OK);
          if (!clas_built) {
            say_refused(2, std::format("the CLAS instantiate was refused (NvAPI {}; {} clusters, {} from templates): units and grass not traced this frame.",
                                       static_cast<int>(instantiated), cluster_count, templated_clusters));
          }
        }
        if (clas_built && cluster_count != templated_clusters) {
          const NvAPI_Status clas_status = NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation(list, &clas_params);
          list->ResourceBarrier(1, &uav_barrier);
          clas_built = (clas_status == NVAPI_OK);
          if (!clas_built) {
            say_refused(4, std::format("the CLAS build was refused (NvAPI {}; {} clusters, {} from templates): units and grass not traced this frame.",
                                       static_cast<int>(clas_status), cluster_count, templated_clusters));
          }
        }
        // The sway's cluster BLASes' lists from the kept CLASes' addresses, counted in their arguments.
        if (sway) {
          const auto counting = Transition(sway_counts_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
          list->ResourceBarrier(1, &counting);
          sway_pass(1, sway_arguments_.Get(), sway_lists_.Get());
          const std::array<D3D12_RESOURCE_BARRIER, 2> counted = {
              Transition(sway_counts_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE), uav_barrier};
          list->ResourceBarrier(static_cast<UINT>(counted.size()), counted.data());
          // Unused root descriptors still point at a buffer.
          list->SetComputeRootShaderResourceView(ROOT_CAPTURE_TABLE, slot->table->GetGPUVirtualAddress());
          for (const Root root : {ROOT_MICROMAP_DATA, ROOT_MICROMAP_INDICES, ROOT_MICROMAP_DESCS}) {
            list->SetComputeRootUnorderedAccessView(root, tlas_scratch_->GetGPUVirtualAddress());
          }
        }
        // The cluster BLASes in one call, an argument each: one call a BLAS with a barrier after each made a battle's grass
        // and units wait for each other (4K harness, units + a grass field: clusters 2.55 -> 2.33 ms). If a driver refuses
        // the batch, one call an argument from then on (once in the log). An empty run's BLAS is never instanced.
        const auto build_blases = [&](uint32_t first_arg, uint32_t args) {
          NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC run_desc = blas_desc;
          if (args == 1) {
            run_desc.inputs.maxArgCount = 1;
            run_desc.indirectArgCount = 0;
          }
          run_desc.destinationAddressArray.StartAddress += uint64_t{first_arg} * 8;
          run_desc.indirectArgArray.StartAddress += uint64_t{first_arg} * 16;
          const NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS run_params = {
              .version = NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS_VER, .pDesc = &run_desc};
          return NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation(list, &run_params);
        };
        if (clas_built && cluster_count != 0 && !cluster_blases_apart_) {
          if (const NvAPI_Status batched = build_blases(0, blas_arg_count); batched == NVAPI_OK) {
            for (uint32_t arg = 0; arg < blas_arg_count; ++arg) cluster_built[arg_blases[arg]] = true;
          } else {
            cluster_blases_apart_ = true;
            slow_report_ += std::format("{}[TW ray tracing] Mega Geometry: the cluster BLASes' batched build refused ({}), one call each now.",
                                        (slow_report_.empty() ? "" : "\n"), static_cast<int>(batched));
          }
        }
        if (clas_built && cluster_count != 0 && cluster_blases_apart_) {
          // The same scratch: a barrier between them.
          for (uint32_t arg = 0; arg < blas_arg_count; ++arg) {
            if (arg != 0) list->ResourceBarrier(1, &uav_barrier);
            const NvAPI_Status blas_status = build_blases(arg, 1);
            cluster_built[arg_blases[arg]] = (blas_status == NVAPI_OK);
            if (blas_status != NVAPI_OK) {
              say_refused(8, std::format("a cluster BLAS's build was refused (NvAPI {}; cluster BLAS {} of {}): its units or grass not traced.",
                                         static_cast<int>(blas_status), arg_blases[arg], CLUSTER_BLASES));
            }
          }
        }
        list->ResourceBarrier(1, &uav_barrier);
        for (size_t blas = 0; blas < CLUSTER_BLASES; ++blas) {
          cluster_built[blas] = cluster_built[blas] && blas_runs[blas].second != blas_runs[blas].first;
        }
        units_clustered = cluster_built[0] || cluster_built[1] || cluster_built[2];
        smoke_clustered = cluster_built[3];
        Stamp(list, slot_index * STAMPS + 5);
        slot_clustered_[slot_index] = true;
        clusters_built = cluster_count;
        templated_built = templated_clusters;
        units_record = first_cluster_record;
      }
#endif
      list->ResourceBarrier(1, &uav_barrier);
      units_record_ = units_record;
      units_masked_ = units_masked || compact_masked;
      // The new placed meshes' build rows (their first instance's) by record (v14); older addons' instanced captures' rows
      // into the slot's readback (four texels an instance), the CPU's once it comes round; the new leaves' micromap
      // inputs (their positions kept, their UVs and mask slice read back: a slice once a request).
      if (!row_copies.empty() || !built_placed.empty() || !micromap_reads.empty()) {
        std::vector<D3D12_RESOURCE_BARRIER> to_source = {
            Transition(vertex_buffer_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
        };
        if (!built_placed.empty()) {
          to_source.push_back(Transition(build_rows_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST));
        }
        if (!micromap_reads.empty()) {
          to_source.push_back(Transition(uvs_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE));
          to_source.push_back(Transition(atlas_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE));
        }
        list->ResourceBarrier(static_cast<UINT>(to_source.size()), to_source.data());
        for (size_t index = 0; index < row_copies.size(); ++index) {
          list->CopyBufferRegion(slot->rows_readback.Get(), uint64_t{row_copies[index].offset} * 64, vertex_buffer_.Get(),
                                 uint64_t{row_sources[index]} * 16, uint64_t{row_copies[index].count} * 64);
        }
        for (const auto& built_rows : built_placed) {
          list->CopyBufferRegion(build_rows_.Get(), uint64_t{meshes_[built_rows.key].record} * 48, vertex_buffer_.Get(),
                                 uint64_t{built_rows.first_vertex} * 16, 48);
        }
        // One buffer of each kind for all of them, their places 256-byte aligned (each read made its own two or three: up to
        // 64 leaves a request, a few ms of allocations, hitches panning over new trees, ATN 2026-10-29).
        constexpr uint64_t MASK_BYTES = uint64_t{dlaa_bridge::RT_ATLAS_SIZE} * dlaa_bridge::RT_ATLAS_SIZE;
        std::map<uint32_t, uint64_t> masks;  // a slice's place in the masks read back
        uint64_t positions_bytes = 0;
        uint64_t uvs_bytes = 0;
        uint64_t gpu_uvs_bytes = 0;
        for (const MicromapRead& read : micromap_reads) {
          if (!read.set) positions_bytes = Align(positions_bytes + (uint64_t{read.triangles} * 48), 256);
          if (read.gpu) {
            gpu_uvs_bytes = Align(gpu_uvs_bytes + (uint64_t{read.triangles} * 12), 256);
            continue;
          }
          uvs_bytes = Align(uvs_bytes + (uint64_t{read.triangles} * 12), 256);
          masks.try_emplace(read.slice, masks.size() * MASK_BYTES);
        }
        std::vector<D3D12_RESOURCE_BARRIER> positions_copied;
        ComPtr<ID3D12Resource> positions;
        ComPtr<ID3D12Resource> uvs;
        ComPtr<ID3D12Resource> mask;
        ComPtr<ID3D12Resource> gpu_uvs;  // GPU Micromap Baking's copies (the sets' UVs are the request's)
        if (!micromap_reads.empty()) {
          if (positions_bytes != 0) {
            positions = Buffer(positions_bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
          }
          if (gpu_uvs_bytes != 0) gpu_uvs = Buffer(gpu_uvs_bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
          if (uvs_bytes != 0) {
            uvs = Buffer(uvs_bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
            mask = Buffer(masks.size() * MASK_BYTES, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
          }
        }
        if ((positions_bytes != 0 && !positions) || (uvs_bytes != 0 && (!uvs || !mask)) || (gpu_uvs_bytes != 0 && !gpu_uvs)) micromap_reads.clear();
        if (!micromap_reads.empty()) {
          const D3D12_RESOURCE_DESC atlas_desc = atlas_->GetDesc();
          for (const auto& [slice, offset] : masks) {
            const D3D12_TEXTURE_COPY_LOCATION destination = {
                .pResource = mask.Get(),
                .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
                .PlacedFootprint = {.Offset = offset,
                                    .Footprint = {.Format = atlas_desc.Format, .Width = dlaa_bridge::RT_ATLAS_SIZE, .Height = dlaa_bridge::RT_ATLAS_SIZE,
                                                  .Depth = 1, .RowPitch = dlaa_bridge::RT_ATLAS_SIZE}},
            };
            const D3D12_TEXTURE_COPY_LOCATION source = {.pResource = atlas_.Get(),
                                                        .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
                                                        .SubresourceIndex = slice * atlas_desc.MipLevels};
            list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
          }
          for (const auto& copied : {positions, gpu_uvs}) {
            if (copied) {
              positions_copied.push_back(Transition(copied.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
            }
          }
        }
        uint64_t positions_at = 0;
        uint64_t uvs_at = 0;
        uint64_t gpu_uvs_at = 0;
        for (const MicromapRead& read : micromap_reads) {
          auto bake = std::make_unique<MicromapBake>();
          *bake = {.key = read.key, .build = read.build, .value = read.gpu ? UINT64_MAX : own_value_ + 1, .triangles = read.triangles,
                   .set = read.set, .grass = read.grass, .cutoff = read.cutoff, .positions = (read.set ? nullptr : positions), .positions_offset = positions_at,
                   .uvs = read.gpu ? gpu_uvs : uvs, .uvs_offset = read.gpu ? gpu_uvs_at : uvs_at, .mask = mask,
                   .mask_offset = read.gpu ? 0u : masks.at(read.slice), .gpu = read.gpu};
          if (!read.set) {
            list->CopyBufferRegion(positions.Get(), positions_at, vertex_buffer_.Get(), uint64_t{read.first_vertex} * 16, uint64_t{read.triangles} * 48);
            positions_at = Align(positions_at + (uint64_t{read.triangles} * 48), 256);
          }
          if (read.gpu) {
            list->CopyBufferRegion(gpu_uvs.Get(), gpu_uvs_at, uvs_.Get(), uint64_t{read.uv_offset} * 4, uint64_t{read.triangles} * 12);
            gpu_uvs_at = Align(gpu_uvs_at + (uint64_t{read.triangles} * 12), 256);
#if defined(RENODX_OMM_SDK)
            // Its mask into a texture of OmmGpu's no other waiting bake holds (one more when they all are).
            auto& gpu_masks = omm_gpu_.masks;
            std::vector<bool> held(gpu_masks.size());
            for (const auto& waiting : micromap_reads_) {
              if (waiting->gpu && waiting->value == UINT64_MAX) held[waiting->mask_index] = true;
            }
            bake->mask_index = static_cast<uint32_t>(std::ranges::find(held, false) - held.begin());
            if (bake->mask_index == gpu_masks.size()) {
              const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
              const D3D12_RESOURCE_DESC texture = {.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D, .Width = dlaa_bridge::RT_ATLAS_SIZE,
                                                   .Height = dlaa_bridge::RT_ATLAS_SIZE, .DepthOrArraySize = 1, .MipLevels = 1,
                                                   .Format = DXGI_FORMAT_R8_UNORM, .SampleDesc = {.Count = 1}};
              ComPtr<ID3D12Resource> made;
              if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                                          nullptr, IID_PPV_ARGS(made.GetAddressOf())))) {
                continue;
              }
              gpu_masks.push_back(std::move(made));
            }
            ID3D12Resource* gpu_mask = gpu_masks[bake->mask_index].Get();
            const auto to_copy = Transition(gpu_mask, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
            list->ResourceBarrier(1, &to_copy);
            const D3D12_TEXTURE_COPY_LOCATION destination = {.pResource = gpu_mask, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
                                                             .SubresourceIndex = 0};
            const D3D12_TEXTURE_COPY_LOCATION source = {.pResource = atlas_.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
                                                        .SubresourceIndex = read.slice * atlas_->GetDesc().MipLevels};
            list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            const auto copied = Transition(gpu_mask, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            list->ResourceBarrier(1, &copied);
#endif
          } else {
            list->CopyBufferRegion(uvs.Get(), uvs_at, uvs_.Get(), uint64_t{read.uv_offset} * 4, uint64_t{read.triangles} * 12);
            uvs_at = Align(uvs_at + (uint64_t{read.triangles} * 12), 256);
          }
#if defined(RENODX_RECORDER)
          // The bake's inputs: read by its BLAS rebuild frames later, by the GPU baker or the CPU (not logged).
          if (uses_) {
            for (ID3D12Resource* input : {bake->positions.Get(), bake->uvs.Get(), bake->mask.Get()}) {
              if (input != nullptr) {
                uses_->External(UseResource(input));
              }
            }
#if defined(RENODX_OMM_SDK)
            if (bake->gpu) {
              uses_->External(UseResource(omm_gpu_.masks[bake->mask_index].Get()));
            }
#endif
          }
#endif
          micromap_reads_.push_back(std::move(bake));
        }
        for (auto& barrier : to_source) std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        to_source.insert(to_source.end(), positions_copied.begin(), positions_copied.end());
        list->ResourceBarrier(static_cast<UINT>(to_source.size()), to_source.data());
        slot->row_reads = std::move(row_copies);
      }
#if defined(RENODX_RECORDER)
      // The journal (RENODX_TW_RECORD): the new static meshes' vertices as their builds read them, the CPU's once the slot
      // comes round (TakeJournalReads); without memory for them a snapshot is refused.
      if (!journal_copies.empty()) {
        std::vector<uint32_t> first_vertices;
        uint64_t journal_bytes = 0;
        for (Slot::JournalRead& read : journal_copies) {
          first_vertices.push_back(static_cast<uint32_t>(read.offset));
          read.offset = journal_bytes;
          journal_bytes += uint64_t{read.vertices} * 16;
        }
        slot->journal_readback = Buffer(journal_bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        if (slot->journal_readback) {
          const auto to_source = Transition(vertex_buffer_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
          list->ResourceBarrier(1, &to_source);
          for (size_t index = 0; index < journal_copies.size(); ++index) {
            list->CopyBufferRegion(slot->journal_readback.Get(), journal_copies[index].offset, vertex_buffer_.Get(), uint64_t{first_vertices[index]} * 16,
                                   uint64_t{journal_copies[index].vertices} * 16);
          }
          const auto copied = Transition(vertex_buffer_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
          list->ResourceBarrier(1, &copied);
          slot->journal_reads = std::move(journal_copies);
        }
      }
#endif
      std::vector<D3D12_RESOURCE_BARRIER> after = {
          Transition(slot->sizes.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
      };
      if (atlas_) after.push_back(Transition(atlas_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON));
      list->ResourceBarrier(static_cast<UINT>(after.size()), after.data());
      list->CopyBufferRegion(slot->readback.Get(), 0, slot->sizes.Get(), 0, slot->compactions.size() * sizeof(uint64_t));
      const auto back = Transition(slot->sizes.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      list->ResourceBarrier(1, &back);
    }
    // What the frame drew.
    const uint32_t drawn_count = std::min(shared.rt_drawn_count, dlaa_bridge::RT_DRAWN);
    for (uint32_t index = 0; index < drawn_count; ++index) {
      if (const auto mesh = meshes_.find(shared.rt_drawn[index]); mesh != meshes_.end()) mesh->second.drawn_frame = frame;
    }
    // The scene (InstanceID: the record of its first geometry).
    UINT instance_count = 0;
    uint64_t triangles = 0;
    uint64_t bytes = 0;
    uint64_t separate_bytes = 0;  // what the static BLASes took as buffers of their own (64 KB at least each)
    for (const auto& [key, mesh] : meshes_) {
      bytes += mesh.bytes;
      separate_bytes += (mesh.bytes + 0xFFFFu) & ~uint64_t{0xFFFFu};
      if (mesh.placed || instance_count == MAX_INSTANCES) continue;
      const bool current = mesh.drawn_frame == frame;
      const bool recent = !mesh.terrain && mesh.drawn_frame + RECENT_FRAMES >= frame;
      if (!current && !recent) continue;
      if (mesh.transforms.empty()) {
        AddInstance(slot, &instance_count, mesh.blas.address, mesh.record, mesh.foliage, nullptr, mesh.record, InstanceMask(mesh));
      } else {
        for (uint32_t instance = 0; instance < mesh.transforms.size() && instance_count < MAX_INSTANCES; ++instance) {
          AddInstance(slot, &instance_count, mesh.blas.address, mesh.record, mesh.foliage, &mesh.transforms[instance], mesh.first_instance + instance,
                      InstanceMask(mesh));
        }
      }
      triangles += mesh.triangles;
    }
    // The pools' chunks logged when they change, against what separate buffers took.
    const auto held = [](const std::vector<BlasChunk>& chunks) { return std::ranges::count_if(chunks, [](const BlasChunk& chunk) { return chunk.buffer != nullptr; }); };
    if (const auto chunks = static_cast<size_t>(held(blas_pools_[BUILD_POOL]) + held(blas_pools_[KEPT_POOL])); details_ && chunks != blas_chunks_logged_) {
      blas_chunks_logged_ = chunks;
      slow_report_ += std::format("{}[TW ray tracing] Static BLASes: {} meshes, {} MB in {} pool chunks of {} MB (as buffers of their own {} MB).",
                                  (slow_report_.empty() ? "" : "\n"), meshes_.size(), bytes >> 20, chunks, BLAS_CHUNK_BYTES >> 20,
                                  separate_bytes >> 20);
    }
    if (units_clustered) {
      // The rest's cluster BLASes (solid but for swaying leaves) and the grass's.
      for (const size_t blas : {size_t{0}, size_t{1}, size_t{2}}) {
        if (!cluster_built[blas] || instance_count >= MAX_INSTANCES) continue;
        AddInstance(slot, &instance_count, cluster_blases_[blas]->GetGPUVirtualAddress(), units_record_, blas == 1 || units_masked_, nullptr,
                    MAX_INSTANCES, (blas == 1) ? GRASS_SWAY_INSTANCE : SOLID_INSTANCE);
      }
      triangles += dynamic_triangles;
    } else if (units_built && instance_count < MAX_INSTANCES) {
      AddInstance(slot, &instance_count, units_blas_->GetGPUVirtualAddress(), units_record_, units_masked_, nullptr, MAX_INSTANCES, SOLID_INSTANCE);
      triangles += dynamic_triangles;
    }
    // The smoke in a TLAS of its own (built after the scene's), its instance the slot's last desc (the CPU's stay far below):
    // its rays (rt_scene.hlsli SmokeTransmittance) walked the scene's TLAS too, ~150 K placed instances in ATN's archer
    // battle (2026-11-01), and its overlapping particles widened the scene's for every ray.
    const bool smoke_scene = (smoke_built || smoke_clustered) && smoke_tlas_ && smoke_tlas_scratch_ && instance_count < MAX_INSTANCES - 1;
    if (smoke_scene) {
      UINT smoke_desc = MAX_INSTANCES - 1;
      AddInstance(slot, &smoke_desc, (smoke_clustered ? cluster_blases_[3] : smoke_blas_)->GetGPUVirtualAddress(), smoke_record, true, nullptr,
                  MAX_INSTANCES, SMOKE_INSTANCE);
    }
    // Placed meshes (v14; not with the partitioned TLAS): rt_place writes their descs after the CPU's instances, this
    // request's draws' (the new captures', then the placements') and those kept from the last dispatch; the TLAS takes
    // place_end descs (past the written ones inactive).
    D3D12_GPU_VIRTUAL_ADDRESS instance_descs = slot->instances->GetGPUVirtualAddress();
    UINT tlas_instances = instance_count;
    uint32_t placed_drawn = 0;
    if (ptlas_ == nullptr && std::ranges::any_of(meshes_, [](const auto& entry) { return entry.second.placed; })) {
      // The records' BLAS addresses (bit 0: drawn moving now, bit 1: grass, bit 2: drawn this frame: from the drawn list
      // when it holds the placed meshes (RT_DRAWN_PLACED) and is not full, else every one; bit 3: all its instances in this
      // request's placements), then the jobs.
      auto* record_blas = reinterpret_cast<uint64_t*>(slot->jobs_data);
      std::fill_n(record_blas, STATIC_RECORDS, uint64_t{0});
      const bool drawn_known = (shared.rt_flags & dlaa_bridge::RT_DRAWN_PLACED) != 0 && drawn_count < dlaa_bridge::RT_DRAWN;
      for (const auto& [key, mesh] : meshes_) {
        if (mesh.placed) {
          record_blas[mesh.record] = mesh.blas.address | (mesh.grass ? 2u : 0u) | ((!drawn_known || mesh.drawn_frame == frame) ? 4u : 0u) | (mesh.moved ? 16u : 0u)
                                     | (mesh.solid_cut_out ? 32u : 0u);
        }
      }
      uint32_t job_count = 0;
      const auto place = [&](const dlaa_bridge::RtPlacement& draw) {
        const auto found = meshes_.find(draw.key);
        if (found == meshes_.end() || !found->second.placed || draw.instances == 0 || job_count == PLACE_JOBS
            || uint64_t{draw.first_vertex} + (uint64_t{draw.instances} * 4u) > vertex_end) {
          return;
        }
        Mesh& mesh = found->second;
        mesh.drawn_frame = frame;
        record_blas[mesh.record] |= 4u;
        if ((draw.flags & dlaa_bridge::RT_PLACE_MOVING) != 0) {
          mesh.moved = true;
          record_blas[mesh.record] |= 17u;
        }
        if ((draw.flags & dlaa_bridge::RT_PLACE_WHOLE) != 0) record_blas[mesh.record] |= 8u;
        const std::array<uint32_t, 8> job = {placed_drawn, draw.instances, draw.first_vertex,
                                             (mesh.foliage ? PLACE_FOLIAGE : 0u) | ((draw.flags & dlaa_bridge::RT_PLACE_HIDDEN) != 0 ? PLACE_HIDDEN : 0u)
                                                 | (mesh.grass ? PLACE_GRASS : 0u) | (mesh.solid_cut_out ? PLACE_CUT_OUT : 0u),
                                             mesh.record, 0u, 0u, 0u};
        std::memcpy(slot->jobs_data + PLACE_TABLE_BYTES + uint64_t{job_count} * 32, job.data(), sizeof(job));
        ++job_count;
        placed_drawn += draw.instances;
        triangles += uint64_t{mesh.triangles} * draw.instances;
      };
      // The shadow pass's rows after the main pass's: rt_place's dedupe keeps the lowest drawn instance at a place.
      for (const auto& built_rows : built_placed) place(built_rows);
      for (const bool cascade : {false, true}) {
        for (uint32_t index = 0; index < placement_count; ++index) {
          if (((shared.rt_placements[index].flags & dlaa_bridge::RT_PLACE_CASCADE) != 0) == cascade) place(shared.rt_placements[index]);
        }
      }
      // All this TLAS can hold: the CPU's, this request's drawn and the last dispatch's descs (the kept ones come from those,
      // so none is cut); no clear and no build past them.
      const auto place_end = static_cast<uint32_t>(std::min<uint64_t>(uint64_t{instance_count} + placed_drawn + placed_written_, placed_capacity_));
      ID3D12Resource* written = placed_instances_[1 - placed_last_].Get();
      const auto to_copy = Transition(written, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
      list->ResourceBarrier(1, &to_copy);
      if (instance_count != 0) {
        list->CopyBufferRegion(written, 0, slot->instances.Get(), 0, uint64_t{instance_count} * sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
      }
      const auto to_place = Transition(written, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      list->ResourceBarrier(1, &to_place);
      Mark(list, L"place");
      list->SetPipelineState(place_pipeline_.Get());
      list->SetComputeRootShaderResourceView(ROOT_SCENE, placed_instances_[placed_last_]->GetGPUVirtualAddress());
      list->SetComputeRootShaderResourceView(ROOT_CAPTURE_TABLE, slot->jobs->GetGPUVirtualAddress());
      list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_DATA, written->GetGPUVirtualAddress());
      list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_INDICES, build_rows_->GetGPUVirtualAddress());
      list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_DESCS, placed_state_->GetGPUVirtualAddress());
      list->SetComputeRootUnorderedAccessView(ROOT_SCRATCH, placed_scratch_->GetGPUVirtualAddress());
      // The camera (b2: grass outside the view stays out) and the sway's flags and range, then clear, claim, count, write
      // (rt_place's modes). Grass keeps its alpha tests (v108 took its micromaps as 2-state: a micro-triangle part blade, part gap went
      // solid, fields black in ATN's debug31; Fast Leaf Shadows still does that for every ray).
      // rt_place's PLACE_ONE_AT_A_PLACE beside the sway's, then Grass Ray Tracing Range (grass past it stays out too).
      const std::array<uint32_t, 4> place_sway = {sway_flags | (((shared.rt_flags & dlaa_bridge::RT_ONE_AT_A_PLACE) != 0) ? 4u : 0u),
                                                  std::bit_cast<uint32_t>(shared.rt_sway_range), placed_capacity_ * 2,
                                                  std::bit_cast<uint32_t>(grass_range)};
      list->SetComputeRoot32BitConstants(ROOT_BOUNCE, 16, shared.rt_view_projection, 0);
      list->SetComputeRoot32BitConstants(ROOT_BOUNCE, static_cast<UINT>(place_sway.size()), place_sway.data(), 16);
      // The items: this request's drawn instances, then (PLACE_WITH_KEPT) the last dispatch's descs (rt_place ItemCount).
      const bool with_kept = (shared.rt_flags & dlaa_bridge::RT_NO_KEPT) == 0;
      const auto items = static_cast<uint32_t>(std::min<uint64_t>(uint64_t{placed_drawn} + (with_kept ? place_end : 0u), uint64_t{placed_capacity_} * 2));
      for (const auto& [mode, threads] : {std::pair{0u, std::max(place_end, placed_capacity_ / 2)}, std::pair{1u, items}, std::pair{2u, items}, std::pair{3u, items}}) {
        // Clearing also takes the dedupe table and its owners, four entries a thread.
        if (threads == 0) continue;
        const std::array<uint32_t, 8> place_dispatch = {mode | (with_kept ? 16u : 0u), job_count, placed_drawn, instance_count, frame, place_end,
                                                        (version >= 24 && shared.rt_kept_frames != 0) ? shared.rt_kept_frames : RECENT_FRAMES,
                                                        STATIC_RECORDS};
        list->SetComputeRoot32BitConstants(ROOT_DISPATCH, static_cast<UINT>(place_dispatch.size()), place_dispatch.data(), 0);
        // Rows of 65535 groups (D3D12's most along x; rt_place's index).
        const uint32_t groups = (threads + 63) / 64;
        list->Dispatch(std::min(groups, 65535u), (groups + 65534) / 65535, 1);
        list->ResourceBarrier(1, &uav_barrier);
      }
      // The counter for the next trace (its room above, its place_end).
      const auto to_read = Transition(placed_state_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
      list->ResourceBarrier(1, &to_read);
      list->CopyBufferRegion(slot->left_out_readback.Get(), LEFT_OUT_BYTES, placed_state_.Get(), 0, sizeof(uint32_t));
      const auto read_done = Transition(placed_state_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      list->ResourceBarrier(1, &read_done);
      placed_slot_ = slot_index;
      const auto to_build = Transition(written, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      list->ResourceBarrier(1, &to_build);
      // Unused root descriptors still point at a buffer.
      list->SetComputeRootShaderResourceView(ROOT_CAPTURE_TABLE, slot->table->GetGPUVirtualAddress());
      for (const Root root : {ROOT_MICROMAP_DATA, ROOT_MICROMAP_INDICES, ROOT_MICROMAP_DESCS}) {
        list->SetComputeRootUnorderedAccessView(root, tlas_scratch_->GetGPUVirtualAddress());
      }
      instance_descs = written->GetGPUVirtualAddress();
      tlas_instances = place_end;
      placed_last_ = 1 - placed_last_;
    }
    if (vertex_end != 0) {
      const auto vertices_written = Transition(vertex_buffer_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
      list->ResourceBarrier(1, &vertices_written);
    }
    uint32_t ptlas_changes = 0;
    Stamp(list, slot_index * STAMPS + 1);
#if defined(RENODX_NVAPI_CLUSTERS)
    if (ptlas_ != nullptr) {
      // The partitioned TLAS: this request's instances by their place (InstanceContributionToHitGroupIndex); a place whose
      // BLAS, InstanceID, flags or transform changed is written, one last request had and this one not written inactive.
      const uint32_t stamp = ++ptlas_stamp_;
      std::vector<uint32_t> places;
      places.reserve(instance_count);
      for (UINT index = 0; index < instance_count; ++index) {
        const D3D12_RAYTRACING_INSTANCE_DESC& instance = slot->instance_data[index];
        const uint32_t at = std::min<uint32_t>(instance.InstanceContributionToHitGroupIndex, PTLAS_INSTANCES - 1);
        PtlasInstance& want = ptlas_wanted_[at];
        want = {.blas = instance.AccelerationStructure,
                .id = instance.InstanceID,
                .flags = NVAPI_D3D12_RAYTRACING_PARTITIONED_TLAS_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE
                         | (((instance.Flags & D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE) != 0)
                                ? NVAPI_D3D12_RAYTRACING_PARTITIONED_TLAS_INSTANCE_FLAG_FORCE_OPAQUE
                                : 0u),
                .stamp = stamp};
        std::memcpy(want.transform.data(), instance.Transform, sizeof(want.transform));
        places.push_back(at);
      }
      auto* writes = reinterpret_cast<NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_OP_ARG_WRITE_INSTANCE*>(slot->ptlas_data + PTLAS_WRITES_OFFSET);
      uint32_t write_count = 0;
      const auto write_place = [&](uint32_t at) {
        PtlasInstance& written = ptlas_written_[at];
        const PtlasInstance& want = ptlas_wanted_[at];
        const bool active = want.stamp == stamp;
        const bool changed = active ? (written.blas != want.blas || written.flags != want.flags || written.id != want.id
                                       || written.transform != want.transform)
                                    : (written.flags != UINT32_MAX && written.blas != 0);
        if (!changed) return;
        auto& write = writes[write_count++];
        write = {};
        if (active) {
          std::memcpy(write.transform, want.transform.data(), sizeof(write.transform));
          write.instanceID = want.id;
          write.instanceFlags = want.flags;
          write.accelerationStructure = want.blas;
          written = want;
        } else {
          write.transform[0][0] = 1.f;
          write.transform[1][1] = 1.f;
          write.transform[2][2] = 1.f;
          written = {.blas = 0, .id = 0, .flags = 0};
        }
        write.instanceMask = 0xFF;
        write.instanceIndex = at;
        write.partitionIndex = at / PTLAS_PARTITION_SIZE;
      };
      for (const uint32_t at : places) write_place(at);
      for (const uint32_t at : ptlas_places_) {
        if (ptlas_wanted_[at].stamp != stamp) write_place(at);
      }
      ptlas_places_ = std::move(places);
      ptlas_changes = write_count;
      auto* operations = reinterpret_cast<NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_OP*>(slot->ptlas_data + PTLAS_OPS_OFFSET);
      const uint64_t upload = slot->ptlas_device->GetGPUVirtualAddress();
      uint32_t operation_count = 0;
      if (write_count != 0) {
        operations[operation_count++] = {.type = NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_OP_WRITE_INSTANCE, .count = write_count,
                                         .data = {.StartAddress = upload + PTLAS_WRITES_OFFSET,
                                                  .StrideInBytes = sizeof(NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_OP_ARG_WRITE_INSTANCE)}};
      }
      std::memcpy(slot->ptlas_data + PTLAS_COUNT_OFFSET, &operation_count, sizeof(operation_count));
      if (operation_count != 0 || !ptlas_built_) {
        const auto copy = [&](uint64_t offset, uint64_t bytes) {
          if (bytes != 0) list->CopyBufferRegion(slot->ptlas_device.Get(), offset, slot->ptlas.Get(), offset, bytes);
        };
        copy(PTLAS_WRITES_OFFSET, uint64_t{write_count} * sizeof(NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_OP_ARG_WRITE_INSTANCE));
        copy(PTLAS_OPS_OFFSET, PTLAS_UPLOAD_BYTES - PTLAS_OPS_OFFSET);
        const auto to_read = Transition(slot->ptlas_device.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        list->ResourceBarrier(1, &to_read);
        ID3D12Resource* next = ptlas_buffers_[1 - ptlas_current_].Get();
        const NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_DESC desc = {
            .inputs = ptlas_inputs_,
            .srcAccelerationStructureData = ptlas_built_ ? ptlas_->GetGPUVirtualAddress() : 0,
            .destAccelerationStructureData = next->GetGPUVirtualAddress(),
            .scratchAccelerationStructureData = ptlas_scratch_->GetGPUVirtualAddress(),
            .indirectOpCount = upload + PTLAS_COUNT_OFFSET,
            .indirectOps = upload + PTLAS_OPS_OFFSET,
        };
        const NVAPI_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_PARAMS params = {.version = NVAPI_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_PARAMS_VER,
                                                                                .pDesc = &desc};
        if (const NvAPI_Status built = NvAPI_D3D12_BuildRaytracingPartitionedTlasIndirect(list, &params); built != NVAPI_OK) {
          return skip(std::format("Ray tracing: the partitioned TLAS failed (NvAPI {})", static_cast<int>(built)));
        }
        const auto to_copy = Transition(slot->ptlas_device.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        list->ResourceBarrier(1, &to_copy);
        ptlas_current_ = 1 - ptlas_current_;
        ptlas_ = next;
        ptlas_built_ = true;
      }
    }
#endif
#if defined(RENODX_RECORDER)
    recorded_scene_ = {.cpu_instances = instance_count, .tlas_instances = tlas_instances,
                       .placed = instance_descs != slot->instances->GetGPUVirtualAddress(), .smoke = smoke_scene, .slot = slot_index};
#endif
    if (ptlas_ == nullptr) {
      const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC top_build = {
          .DestAccelerationStructureData = tlas_->GetGPUVirtualAddress(),
          .Inputs = {.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
                     .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD,
                     .NumDescs = tlas_instances,
                     .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
                     .InstanceDescs = instance_descs},
          .ScratchAccelerationStructureData = tlas_scratch_->GetGPUVirtualAddress(),
      };
      Mark(list, L"TLAS");
      list->BuildRaytracingAccelerationStructure(&top_build, 0, nullptr);
    }
    if (smoke_scene) {
      const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC smoke_build = {
          .DestAccelerationStructureData = smoke_tlas_->GetGPUVirtualAddress(),
          .Inputs = {.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
                     .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
                     .NumDescs = 1,
                     .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
                     .InstanceDescs = slot->instances->GetGPUVirtualAddress() + (uint64_t{MAX_INSTANCES - 1} * sizeof(D3D12_RAYTRACING_INSTANCE_DESC))},
          .ScratchAccelerationStructureData = smoke_tlas_scratch_->GetGPUVirtualAddress(),
      };
      Mark(list, L"smoke TLAS");
      list->BuildRaytracingAccelerationStructure(&smoke_build, 0, nullptr);
    }
    Stamp(list, slot_index * STAMPS + 2);
    // The shared textures: COMMON between the APIs, read or written here.
    std::vector<std::pair<ID3D12Resource*, D3D12_RESOURCE_STATES>> textures = {
        {positions_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
        {sun_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
        {visibility_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
        {ambient_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
    };
    if (atlas_) textures.emplace_back(atlas_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    // The local lights (v9): their positions and reaches to b4, their lit bits out.
    const uint32_t light_count = SharedLightCount(shared, version);
    const bool local_lights = (shared.rt_flags & dlaa_bridge::RT_LOCAL_LIGHTS) != 0 && light_mask_ && light_count != 0;
    // The fog shadows (v11): a quarter of the render pixels each way.
    const bool fog = (shared.rt_flags & dlaa_bridge::RT_FOG_SHADOWS) != 0 && fog_;
    if (fog) textures.emplace_back(fog_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (fog && fog_lights_ && (shared.rt_flags & dlaa_bridge::RT_FOG_LIGHTS) != 0) {
      textures.emplace_back(fog_lights_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    // Coloured Shadows (v19, with Light Through Leaves): the sun's filter through cloth and sails, their colour from Ray
    // Reconstruction's diffuse albedo guide (last frame's: written after this frame's sun draws) where the screen shows them.
    constexpr uint32_t COLOURED = dlaa_bridge::RT_COLOURED_SHADOWS | dlaa_bridge::RT_LEAF_LIGHT;
    const bool tint = tint_ && version >= 19 && (shared.rt_flags & COLOURED) == COLOURED;
    const bool tint_guides = tint && guide_albedo_ && shared.rt_guides != 0;
    if (tint) textures.emplace_back(tint_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (tint_guides) textures.emplace_back(guide_albedo_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    // Their places in the last trace's list (the addon orders them by importance each frame; the same position and
    // reach: the same light), a byte each after the colours (255: new).
    std::array<uint8_t, dlaa_bridge::RT_ALL_LIGHTS> light_previous = {};
    if (local_lights) {
      textures.emplace_back(light_mask_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      // With their colours for the fog's torch and fire light (rt_fog; the paths' requests copy them too).
      CopyLights(shared, version, light_count, slot->light_data);
      for (uint32_t light = 0; light < light_count; ++light) {
        const float* position = SharedLight(shared, light, false);
        const auto found = std::ranges::find_if(previous_lights_.begin(), previous_lights_.begin() + previous_light_count_,
                                                [&](const std::array<float, 4>& other) {
                                                  return std::equal(other.begin(), other.end(), position);
                                                });
        light_previous[light] = (found != previous_lights_.begin() + previous_light_count_)
                                    ? static_cast<uint8_t>(found - previous_lights_.begin())
                                    : uint8_t{255};
      }
      std::memcpy(slot->light_data + LIGHT_PREVIOUS_OFFSET, light_previous.data(), light_previous.size());
    }
    std::vector<D3D12_RESOURCE_BARRIER> barriers = {uav_barrier};
    for (const auto& [resource, used] : textures) {
      barriers.push_back(Transition(resource, D3D12_RESOURCE_STATE_COMMON, used));
    }
    list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    list->SetComputeRootShaderResourceView(ROOT_SCENE, (ptlas_ != nullptr ? ptlas_ : tlas_.Get())->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(ROOT_SMOKE, (smoke_scene ? smoke_tlas_.Get() : (ptlas_ != nullptr ? ptlas_ : tlas_.Get()))->GetGPUVirtualAddress());
    const float softness = (version >= 6) ? std::clamp(shared.rt_sun_softness, 0.f, 5.000012f) : 0.f;
    const bool soft = (shared.rt_flags & dlaa_bridge::RT_SUN_SHADOWS) != 0 && softness > 0.01f;
    // Under Ray Reconstruction no soft-shadow filter (rt_trace jitters its sun ray). AO only reaches RR multiplied into the
    // game's colour, and RR is no AO denoiser: the ambient keeps its history (the harness's RR test left 19-23% of the raw
    // 4-ray AO's noise, a third of that with it), or REBLUR cleans it first when the addon asks (RT_AO_REBLUR).
    const bool reconstructed = (shared.rt_flags & dlaa_bridge::RT_RECONSTRUCTED) != 0;
    const bool ambient_by_nrd = (shared.rt_flags & (reconstructed ? dlaa_bridge::RT_AO_REBLUR : dlaa_bridge::RT_NRD)) != 0;
    // The Denoiser setting at NRD: SIGMA in place of the soft-shadow filter, REBLUR in place of the ambient's history. Any
    // signal unused for a while gives its memory back.
    bool nrd_sun = false;
    bool nrd_ambient = false;
#if defined(RENODX_NRD)
    for (NrdSignal* signal : {&nrd_sun_, &nrd_ambient_, &nrd_bounce_, &nrd_reflections_}) {
      if (signal->denoiser && signal->used_frame + NRD_RELEASE_FRAMES < frame) {
        if (!WaitFor(own_value_)) break;
        signal->denoiser.reset();
        slow_report_ += std::format("{}[TW ray tracing] An NRD denoiser released (unused for {} frames).", slow_report_.empty() ? "" : "\n", NRD_RELEASE_FRAMES);
      }
    }
    if (ambient_by_nrd) {
      const auto nrd_width = static_cast<uint32_t>(visibility_->GetDesc().Width);
      const uint32_t nrd_height = visibility_->GetDesc().Height;
      static constexpr std::array<NrdTexture, 5> SUN_TEXTURES = {{
          {nrd::ResourceType::IN_VIEWZ, DXGI_FORMAT_R32_FLOAT, NRD_SUN_VIEW_Z},
          {nrd::ResourceType::IN_NORMAL_ROUGHNESS, DXGI_FORMAT_R10G10B10A2_UNORM, NRD_SUN_NORMAL_ROUGHNESS},
          {nrd::ResourceType::IN_PENUMBRA, DXGI_FORMAT_R16_FLOAT, NRD_SUN_PENUMBRA},
          {nrd::ResourceType::IN_TRANSLUCENCY, DXGI_FORMAT_R8G8B8A8_UNORM, NRD_SUN_TRANSLUCENCY},
          {nrd::ResourceType::OUT_SHADOW_TRANSLUCENCY, DXGI_FORMAT_R8G8B8A8_UNORM, 0},
      }};
      static constexpr std::array<NrdTexture, 4> AMBIENT_TEXTURES = {{
          {nrd::ResourceType::IN_VIEWZ, DXGI_FORMAT_R32_FLOAT, NRD_AMBIENT_VIEW_Z},
          {nrd::ResourceType::IN_NORMAL_ROUGHNESS, DXGI_FORMAT_R10G10B10A2_UNORM, NRD_AMBIENT_NORMAL_ROUGHNESS},
          {nrd::ResourceType::IN_DIFF_HITDIST, DXGI_FORMAT_R8_UNORM, NRD_AMBIENT_SIGNAL},
          {nrd::ResourceType::OUT_DIFF_HITDIST, DXGI_FORMAT_R8_UNORM, 0},
      }};
      nrd_sun = !reconstructed && soft && EnsureNrd(&nrd_sun_, nrd::Denoiser::SIGMA_SHADOW_TRANSLUCENCY, "NRD SIGMA", {RT_NRD_FRONT_SUN, sizeof(RT_NRD_FRONT_SUN)},
                                  {RT_NRD_FINISH_SUN, sizeof(RT_NRD_FINISH_SUN)}, nrd_width, nrd_height, SUN_TEXTURES, frame);
      nrd_ambient = (shared.rt_flags & dlaa_bridge::RT_AMBIENT_OCCLUSION) != 0
                    && EnsureNrd(&nrd_ambient_, nrd::Denoiser::REBLUR_DIFFUSE_OCCLUSION, "NRD REBLUR (AO)", {RT_NRD_FRONT_AMBIENT, sizeof(RT_NRD_FRONT_AMBIENT)},
                                 {RT_NRD_FINISH_AMBIENT, sizeof(RT_NRD_FINISH_AMBIENT)}, nrd_width, nrd_height, AMBIENT_TEXTURES, frame);
    }
#endif
    // Torch and Fire Light in the Light Bounce (helper111): the paths take the first 32 lights; rt_lights leaves them dark
    // (only after a bounce that carried them), or with RT_LIGHT_RATIO (Torch and Fire Shadows from the Light Bounce) writes
    // the last bounce's shadow ratio (whenever asked: the game's light shaders read the words as ratios; 1 until one is kept).
    const bool lights_asked = local_lights && lights_bounce_pipeline_ && ((shared.rt_flags & dlaa_bridge::RT_LIGHTS_IN_BOUNCE) != 0 || LightsInBounceTest() != 0);
    const bool light_ratio = lights_asked && ((shared.rt_flags & dlaa_bridge::RT_LIGHT_RATIO) != 0 || LightsInBounceTest() == 3u);
    const bool lights_in_bounce = light_ratio || (lights_asked && reuse_lights_mode_ != 0 && reuse_frame_ != UINT32_MAX && reuse_frame_ + 1 >= frame);
    const uint32_t lights_mode = light_ratio ? 3u : (lights_in_bounce ? 1u : 0u);
    const bool ratio_kept = light_ratio && light_ratio_[0] && EnsureHistory(HISTORY_RATIO, frame) && ratio_frame_ != UINT32_MAX && ratio_frame_ + 1 == frame;
    // Its pixels the ratio can't serve traced, their own bits (the addon's light shaders read them: RT_SCENE_LIGHT_BITS).
    const bool ratio_bits = light_ratio && ((shared.rt_scene_flags & dlaa_bridge::RT_SCENE_LIGHT_BITS) != 0 || LightsInBounceTest() == 3u);
    // v18: rays a pixel toward the sun and each light, the lights' radius for soft shadows (rt_lights: under RR).
    const std::array<uint32_t, 12> settings = {
        frame,
        (shared.rt_flags & ~(dlaa_bridge::RT_LOCAL_LIGHTS | dlaa_bridge::RT_NRD | dlaa_bridge::RT_AO_REBLUR | dlaa_bridge::RT_START_PAST | dlaa_bridge::RT_LIGHTS_IN_BOUNCE
                             | dlaa_bridge::RT_LIGHT_RATIO))
            | (local_lights ? dlaa_bridge::RT_LOCAL_LIGHTS : 0u) | (lights_in_bounce ? dlaa_bridge::RT_LIGHTS_IN_BOUNCE : 0u) | (light_ratio ? dlaa_bridge::RT_LIGHT_RATIO : 0u)
            | (smoke_scene ? TRACE_SMOKE : 0u) | (nrd_sun ? dlaa_bridge::RT_NRD : 0u) | (nrd_ambient ? TRACE_NRD_AMBIENT : 0u),
        std::bit_cast<uint32_t>(std::clamp(shared.rt_ambient_range, 0.1f, 100.02711f)),
        std::clamp(shared.rt_ambient_rays, 1u, 16u),
        shared.rt_width,
        shared.rt_height,
        std::bit_cast<uint32_t>(soft ? std::tan(softness * 3.14159265f / 180.f) : 0.f),
        (version >= 18) ? std::clamp(shared.rt_shadow_rays, 1u, 8u) : 1u,
        std::bit_cast<uint32_t>((version >= 18) ? std::clamp(shared.rt_light_size, 0.f, 2.f) : 0.f),
        std::bit_cast<uint32_t>((version >= 18) ? std::clamp(shared.rt_grass_shadow_range, 0.f, 1.0001666e4f) : 0.f),
        // v31: the light mask's tiles' first row (rt_lights).
        (version >= 31) ? shared.rt_light_tile_row : 0u,
    };
    list->SetComputeRoot32BitConstants(ROOT_SETTINGS, static_cast<UINT>(settings.size()), settings.data(), 0);
    list->SetComputeRootConstantBufferView(ROOT_LIGHTS, slot->lights->GetGPUVirtualAddress());
    // The trace's history: the ambient visibility and the local lights' bits kept over frames, valid when the last trace
    // was the frame before (its positions and camera kept for the projection; the lights' bits also with the same Torch and
    // Fire Light in the Light Bounce: its lights' kept bits were never traced). Its constants bound every trace.
    const bool lights_kept = local_lights && version >= 11 && EnsureHistory(HISTORY_LIGHTS, frame);
    const bool ambient_kept = version >= 11 && (((shared.rt_flags & dlaa_bridge::RT_AMBIENT_OCCLUSION) != 0 && !nrd_ambient) || lights_kept)
                              && EnsureHistory(HISTORY_AMBIENT, frame);
    // The last trace's positions: ambient_positions_, or kept_positions_' slice trace_positions_slice_ where that trace's copy
    // was the paths' too. This trace's copy goes to the paths' next slice when the frame's Light Bounce will take it (one
    // took the last frame's: Paths takes this one), else to ambient_positions_: one render-size RGBA32F copy a frame less
    // with both histories.
    const bool history_kept = ambient_kept && kept_positions_ && trace_positions_slice_ != NO_SLICE;
    const bool copy_kept = ambient_kept && kept_positions_ && copied_frame_ != frame && light_frame_ != UINT32_MAX && light_frame_ + 1 == frame;
    if (history_kept || copy_kept) history_used_[HISTORY_PATHS] = frame;
#if defined(RENODX_REGIR)
    // Torch Light in Path-Traced GI's T1 (PT DI step 3): rt_lights' torch light at the render pixels while the row's paths
    // asked within HISTORY_FRAMES (ExpireHistory gives it back after), for this frame's paths (torch_irradiance_frame_).
    const bool torch_written = local_lights && lights_torch_pipelines_[0] && torch_light_frame_ != UINT32_MAX && torch_light_frame_ + HISTORY_FRAMES >= frame
                               && (torch_irradiance_ || Texture(DXGI_FORMAT_R16G16B16A16_FLOAT, &torch_irradiance_));
    if (torch_written) {
      Written(torch_irradiance_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, TORCH_IRRADIANCE);
      torch_irradiance_frame_ = frame;
    }
#else
    constexpr bool torch_written = false;
#endif
    {
      // RENODX_TW_NO_TRACE_HISTORY=1 (the quality scorer's raw AO and torches with every other history kept: frame numbers
      // two apart turned off the paths' and the reflections' too): neither history is applied, both still written.
      static const bool no_trace_history = GetEnvironmentVariableW(L"RENODX_TW_NO_TRACE_HISTORY", nullptr, 0) != 0;
      const bool last_frame = !no_trace_history && ambient_kept && ambient_frame_ != UINT32_MAX && ambient_frame_ + 1 == frame;
      PathConstants constants = {.flags = (last_frame ? PATH_AMBIENT_HISTORY : 0u) | (((shared.rt_flags & dlaa_bridge::RT_START_PAST) != 0) ? PATH_START_PAST : 0u)
                                          | (lights_kept && last_frame && lights_frame_ + 1 == frame && lights_mode == lights_mode_traced_ ? PATH_LIGHT_HISTORY : 0u)
                                          | (ambient_parity_ != 0 ? PATH_AMBIENT_PARITY : 0u) | (tint ? PATH_TINT : 0u)
                                          | (tint_guides ? PATH_TINT_GUIDES : 0u) | (history_kept ? PATH_AMBIENT_KEPT : 0u),
                                 .previous_positions_slice = history_kept ? trace_positions_slice_ : 0u,
                                 // The last bounce's shadow ratio: its texels, the set it wrote.
                                 .lights_in_bounce = (light_ratio ? (3u | ((ratio_parity_ == 0) ? 16u : 0u) | (ratio_kept ? 32u : 0u) | (ratio_bits ? LIGHTS_RATIO_BITS : 0u)) : 0u)};
      if (light_ratio) {
        constants.bounce_width = ratio_size_[0];
        constants.bounce_height = ratio_size_[1];
        constants.bounce_scale = ratio_size_[2];
      }
      std::copy_n(shared.rt_view_projection, 16, constants.view_projection.begin());
      constants.previous_view_projection = ambient_view_projection_;
      std::memcpy(slot->paths_data, &constants, sizeof(constants));
      list->SetComputeRootConstantBufferView(ROOT_PATHS, slot->paths->GetGPUVirtualAddress());
    }
    // The last trace's positions for the trace's and the local lights' dispatches.
    D3D12_RESOURCE_BARRIER history_read = Transition(ambient_positions_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (history_kept) {
      history_read = Transition(kept_positions_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      history_read.Transition.Subresource = trace_positions_slice_;
    }
    if (ambient_kept) list->ResourceBarrier(1, &history_read);
    Mark(list, L"trace");
    list->SetPipelineState(trace_pipeline_.Get());
    if (profiling_) {
      // The profile's split: the sun's part, then the ambient's (rt_trace's PART_*), timed apart.
      list->SetComputeRoot32BitConstant(ROOT_SETTINGS, settings[1] | TRACE_PART_SUN, 1);
      list->Dispatch((shared.rt_width + 7) / 8, (shared.rt_height + 7) / 8, 1);
      ProfileStamp(list, slot_index, 0);
      list->SetComputeRoot32BitConstant(ROOT_SETTINGS, settings[1] | TRACE_PART_AMBIENT, 1);
      list->Dispatch((shared.rt_width + 7) / 8, (shared.rt_height + 7) / 8, 1);
      ProfileStamp(list, slot_index, 1);
      list->SetComputeRoot32BitConstant(ROOT_SETTINGS, settings[1], 1);
    } else {
      list->Dispatch((shared.rt_width + 7) / 8, (shared.rt_height + 7) / 8, 1);
    }
    // The local lights' shadows (rt_lights: their own registers; before this trace's positions become the history), its
    // twin with T1 while torch_written.
    if (local_lights) {
      Mark(list, L"local lights");
      const ComPtr<ID3D12PipelineState>& lights_state = (torch_written ? lights_torch_pipelines_[lights_in_bounce ? 1 : 0]
                                                                       : (lights_in_bounce ? lights_bounce_pipeline_ : lights_pipeline_));
      list->SetPipelineState(lights_state.Get());
      list->Dispatch((shared.rt_width + 7) / 8, (shared.rt_height + 7) / 8, 1);
    }
    if (ambient_kept) {
      // This trace's positions for the next one (copy_kept: in the slices' turn, as the paths' first request's copy).
      std::swap(history_read.Transition.StateBefore, history_read.Transition.StateAfter);
      list->ResourceBarrier(1, &history_read);
      if (copy_kept) {
        previous_positions_slice_ = copied_slice_;
        copied_slice_ = 1u - copied_slice_;
        copied_frame_ = frame;
      }
      ID3D12Resource* const kept = copy_kept ? kept_positions_.Get() : ambient_positions_.Get();
      const UINT kept_subresource = copy_kept ? copied_slice_ : 0u;
      std::array<D3D12_RESOURCE_BARRIER, 2> to_copy = {
          Transition(kept, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST),
          Transition(positions_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
      };
      to_copy[0].Transition.Subresource = kept_subresource;
      list->ResourceBarrier(static_cast<UINT>(to_copy.size()), to_copy.data());
      const D3D12_TEXTURE_COPY_LOCATION to = {.pResource = kept, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, .SubresourceIndex = kept_subresource};
      const D3D12_TEXTURE_COPY_LOCATION from = {.pResource = positions_.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
      const D3D12_BOX region = {.left = 0, .top = 0, .front = 0, .right = std::max(shared.rt_width, 1u), .bottom = std::max(shared.rt_height, 1u),
                                .back = 1};
      list->CopyTextureRegion(&to, 0, 0, 0, &from, &region);
      for (auto& barrier : to_copy) std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
      list->ResourceBarrier(static_cast<UINT>(to_copy.size()), to_copy.data());
      trace_positions_slice_ = copy_kept ? copied_slice_ : NO_SLICE;
      std::copy_n(shared.rt_view_projection, 16, ambient_view_projection_.begin());
      ambient_frame_ = frame;
      ambient_parity_ ^= 1u;
    }
    ProfileStamp(list, slot_index, 2);
    // This trace's lights for the next one's places.
    if (lights_kept) {
      for (uint32_t light = 0; light < light_count; ++light) std::copy_n(SharedLight(shared, light, false), 4, previous_lights_[light].begin());
      previous_light_count_ = light_count;
      lights_frame_ = frame;
      lights_mode_traced_ = lights_mode;
    }
    if (fog) {
      Mark(list, L"fog");
      list->SetPipelineState(fog_pipeline_.Get());
      list->Dispatch((shared.rt_width + 31) / 32, (shared.rt_height + 31) / 32, 1);
    }
    bool nrd_denoised = false;
#if defined(RENODX_NRD)
    // NRD's inputs on this list's bindings; SIGMA and REBLUR take theirs (before the built-in filter only when one failed
    // before touching the list: a camera it cannot take apart, no such frame known).
    if (nrd_sun || nrd_ambient) {
      list->ResourceBarrier(1, &uav_barrier);
      Mark(list, L"NRD inputs");
      for (NrdSignal* signal : {nrd_sun ? &nrd_sun_ : nullptr, nrd_ambient ? &nrd_ambient_ : nullptr}) {
        if (signal == nullptr) continue;
        signal->denoiser->ToWritten(list);
        list->SetPipelineState(signal->front.Get());
        list->Dispatch((shared.rt_width + 7) / 8, (shared.rt_height + 7) / 8, 1);
      }
      list->ResourceBarrier(1, &uav_barrier);
    }
    if (nrd_sun) {
      // SIGMA's light direction (toward the sun, world space; else an isotropic blur, softer and blotchier from a low sun):
      // the sun texture's for a later trace.
      auto to_copy = Transition(sun_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
      list->ResourceBarrier(1, &to_copy);
      const D3D12_TEXTURE_COPY_LOCATION sun_copy = {
          .pResource = slot->sun_readback.Get(),
          .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
          .PlacedFootprint = {.Footprint = {.Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .Width = 1, .Height = 1, .Depth = 1, .RowPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT}},
      };
      const D3D12_TEXTURE_COPY_LOCATION sun_texture = {.pResource = sun_.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
      const D3D12_BOX toward_texel = {.left = 0, .top = 0, .front = 0, .right = 1, .bottom = 1, .back = 1};
      list->CopyTextureRegion(&sun_copy, 0, 0, 0, &sun_texture, &toward_texel);
      std::swap(to_copy.Transition.StateBefore, to_copy.Transition.StateAfter);
      list->ResourceBarrier(1, &to_copy);
      slot->sun_copied = true;
      Mark(list, L"NRD SIGMA");
      const nrd::SigmaSettings sigma = {.lightDirection = {sun_toward_[0], sun_toward_[1], sun_toward_[2]}};
      nrd_denoised = nrd_sun_.denoiser->Run(list, static_cast<UINT>(slot_index), shared.rt_view_projection, frame, shared.rt_width, shared.rt_height, &sigma);
      if (nrd_denoised) {
        nrd_sun_.denoiser->Finish(list, nrd::ResourceType::OUT_SHADOW_TRANSLUCENCY, positions_.Get(), POSITIONS_VIEW, visibility_.Get(), DXGI_FORMAT_R8_UNORM);
      }
    }
#endif
    if (soft && !reconstructed && !nrd_denoised) {
      // Soft shadows: the rays' penumbras.
      list->ResourceBarrier(1, &uav_barrier);
      Mark(list, L"soft");
      list->SetPipelineState(soft_pipeline_.Get());
      list->Dispatch((shared.rt_width + 7) / 8, (shared.rt_height + 7) / 8, 1);
    }
    bool nrd_ambient_ran = false;
#if defined(RENODX_NRD)
    if (nrd_ambient) {
      Mark(list, L"NRD REBLUR (AO)");
      // Its hit distance is the rays' open share of their range.
      const nrd::ReblurSettings reblur = {.hitDistanceParameters = {.A = std::clamp(shared.rt_ambient_range, 0.1f, 100.f), .B = 0.f, .C = 1.f}};
      nrd_ambient_ran = nrd_ambient_.denoiser->Run(list, static_cast<UINT>(slot_index), shared.rt_view_projection, frame, shared.rt_width, shared.rt_height, &reblur);
      if (nrd_ambient_ran) {
        nrd_ambient_.denoiser->Finish(list, nrd::ResourceType::OUT_DIFF_HITDIST, positions_.Get(), POSITIONS_VIEW, ambient_.Get(), DXGI_FORMAT_R8_UNORM);
      }
    }
#endif
    if (ambient_by_nrd && (shared.rt_flags & dlaa_bridge::RT_AMBIENT_OCCLUSION) != 0) {
      NoteNrd(dlaa_bridge::RT_NRD_OFF_AMBIENT, nrd_ambient_ran);
    }
#if defined(RENODX_REFERENCE)
    ReferenceRequest(list, {.kind = ReferenceKind::TRACE, .frame = frame, .settings = &settings, .capture_table = slot->table->GetGPUVirtualAddress(),
                            .instance_descs = instance_descs, .instances = tlas_instances});
#endif
    barriers.clear();
    for (const auto& [resource, used] : textures) {
      barriers.push_back(Transition(resource, used, D3D12_RESOURCE_STATE_COMMON));
    }
    list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    Stamp(list, slot_index * STAMPS + 3);
    // The waiting GPU bakes, oldest first, within the request's budget (the SDK's guide asks for one: MICROMAP_GPU_BAKES
    // and MICROMAP_GPU_TRIANGLES, the first whatever its size); one whose mesh or cluster set changed since dropped.
    std::vector<MicromapBake*> gpu_baking;
    uint32_t gpu_bake_triangles = 0;
    for (std::unique_ptr<MicromapBake>& bake : micromap_reads_) {
      if (!bake->gpu || bake->value != UINT64_MAX) continue;
      const auto mesh = meshes_.find(bake->key);
      const auto set = sets_.find(bake->key);
      const bool current = (bake->set ? (set != sets_.end() && set->second.generation == bake->build)
                                      : (mesh != meshes_.end() && mesh->second.build == bake->build));
      if (!current) {
        bake.reset();
        continue;
      }
      if (gpu_baking.size() == MICROMAP_GPU_BAKES || (!gpu_baking.empty() && gpu_bake_triangles + bake->triangles > MICROMAP_GPU_TRIANGLES)) break;
      bake->value = own_value_ + 1;
      gpu_bake_triangles += bake->triangles;
      gpu_baking.push_back(bake.get());
    }
    std::erase(micromap_reads_, nullptr);
    slot->gpu_bakes = static_cast<uint32_t>(gpu_baking.size());
    for (const MicromapBake* bake : gpu_baking) {
      if (details_) {
        slot->micromap_builds += std::format("; GPU bake {} triangles{}", bake->triangles, bake->set ? ", grass set" : "");
      }
    }
    // Before the game's signal, so the game waits for them (MICROMAP_GPU_BAKES keeps that short). In a list of their own after
    // it, beside the game's frame, the GPU reset in 3 runs of 5 at their dispatches; here in none of 8 (ATN's freezes).
#if defined(RENODX_OMM_SDK)
    BakeMicromapsOnGpu(list, gpu_baking);
#endif
    Stamp(list, slot_index * STAMPS + 9);
    Resolve(list, slot_index);
    if (const HRESULT closed = list->Close(); FAILED(closed)) {
      request_failed_ = true;
      status_ = std::format("Ray tracing: a request failed (0x{:08X})", static_cast<uint32_t>(closed));
      // Which passes it held (the one-off E_INVALIDARG of debug32 recorded in 0.01 ms; with RENODX_TW_D3D12_DEBUG the
      // debug layer's messages follow).
      slow_report_ += std::format("{}[TW ray tracing] Trace {} could not be recorded (0x{:08X}; {} captures, {} compact draws, {} placements, {} GPU "
                                  "bakes); its passes: {}.",
                                  slow_report_.empty() ? "" : "\n", own_value_ + 1, static_cast<uint32_t>(closed), shared.rt_capture_count,
                                  shared.rt_compact_count, shared.rt_placement_count, slot->gpu_bakes, Passes(*slot));
      return false;
    }
    // Meshes unseen for long give their BLAS (and micromaps, record, pool ranges) back once the GPU is past this request.
    std::erase_if(meshes_, [&](auto& entry) {
      const uint64_t release_frames = (entry.second.placed && version >= 24 && shared.rt_kept_frames != 0) ? shared.rt_kept_frames : RELEASE_FRAMES;
      if (entry.second.drawn_frame + release_frames >= frame) return false;
      ReleaseMesh(&entry.second);
      return true;
    });
    if (!details_) return true;
    std::string denoisers;
#if defined(RENODX_NRD)
    denoisers = std::format("{}{}{}{}", nrd_denoised ? ", NRD SIGMA" : "", nrd_ambient ? ", NRD AO" : "", nrd_bounce_.denoiser ? ", NRD Light Bounce" : "",
                            nrd_reflections_.denoiser ? ", NRD reflections" : "");
#endif
    status_ = std::format("Ray tracing ({}{}): every frame {:.2f} M triangles (terrain {:.2f}, grass and leaves {:.2f}; {}) | {} meshes ({} new, {} "
                          "micromaps, {} MB), {} + {} placed instances ({} with those kept){}, {:.1f} M triangles",
                          dxr12_ ? "DXR 1.2" : "DXR 1.1", denoisers, static_cast<double>((units_built || units_clustered) ? dynamic_triangles : 0u) / 1.0e6,
                          static_cast<double>((units_built || units_clustered) ? terrain_triangles : 0u) / 1.0e6,
                          static_cast<double>((units_built || units_clustered) ? grass_triangles : 0u) / 1.0e6,
                          units_clustered ? std::format("{} clusters ({} from templates), {} compact draws of {} sets", clusters_built, templated_built,
                                                        compact_drawn, sets_.size())
                                          : (units_built ? (units_refitted ? "refitted" : "rebuilt") : "none"),
                          meshes_.size(), built, micromaps_built, bytes >> 20, instance_count, placed_drawn, placed_written_,
                          ptlas_ != nullptr ? std::format(" (partitioned TLAS: {} changed)", ptlas_changes) : std::string(),
                          static_cast<double>(triangles) / 1.0e6);
    profile_counts_ = std::format(",{:.3f},{:.3f},{:.3f},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
                                  static_cast<double>((units_built || units_clustered) ? dynamic_triangles : 0u) / 1.0e6,
                                  static_cast<double>((units_built || units_clustered) ? terrain_triangles : 0u) / 1.0e6,
                                  static_cast<double>((units_built || units_clustered) ? grass_triangles : 0u) / 1.0e6, clusters_built, templated_built,
                                  compact_drawn, sets_.size(), new_set_count, template_builds, meshes_.size(), built, micromaps_built,
                                  micromap_reads_.size() + micromap_queue_.size() + micromaps_baked_.size(), instance_count, placed_drawn,
                                  cluster_room_, bytes >> 20);
    if (slot->stamped) {
      gpu_timings_ = std::format("GPU ms: scene {:.2f} (clusters {:.2f}), TLAS {:.2f}, trace {:.2f}, bounce {:.2f}, reflections {:.2f}", gpu_ms_[0],
                             gpu_ms_[5], gpu_ms_[1], gpu_ms_[2], gpu_ms_[3], gpu_ms_[4]);
    }
    slot->status = status_;
    return true;
  }

  // QueryPerformanceCounter's ticks from `from` to `to` in milliseconds.
  static double CpuMs(const LARGE_INTEGER& from, const LARGE_INTEGER& to) {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return static_cast<double>(to.QuadPart - from.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
  }

  // A slot's request's profile line (RT_PROFILE) with its GPU times, once they came back.
  void WriteProfile(size_t slot_index, const std::string& gpu_times) {
    std::string& line = slots_[slot_index].profile;
    if (line.empty()) return;
    if (profile_.is_open()) {
      profile_ << line << gpu_times << ',' << slots_[slot_index].profile_parts << '\n';
      if (!profiling_) profile_.flush();
    }
    line.clear();
  }

  // GPU times (with Log Details or the profile, but with RENODX_TW_NO_GPU_TIMES): a timestamp at `index`, a slot's resolved
  // for reading, its last read back.
  void Stamp(ID3D12GraphicsCommandList* list, size_t index) const {
    if (timestamps_ && details_) {
      list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, static_cast<UINT>(index));
    }
  }
  // All of a slot's timestamps at its list's start (Resolve reads them all; the debug layer flagged the ones a request
  // never wrote, every request).
  void StampSlot(ID3D12GraphicsCommandList* list, size_t slot_index) const {
    for (size_t index = 0; index < STAMPS; ++index) Stamp(list, (slot_index * STAMPS) + index);
  }
  // A request's profile part's end (RT_PROFILE): after the passes before it (a UAV barrier: they overlap otherwise).
  void ProfileStamp(ID3D12GraphicsCommandList* list, size_t slot_index, size_t part) const {
    if (!profiling_ || !timestamps_) return;
    const D3D12_RESOURCE_BARRIER uav_barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV};
    list->ResourceBarrier(1, &uav_barrier);
    Stamp(list, (slot_index * STAMPS) + 6 + part);
  }
  void Resolve(ID3D12GraphicsCommandList* list, size_t slot_index) {
#if defined(RENODX_RECORDER)
    ServeStamp(list, SERVE_END);
#endif
    slots_[slot_index].stamped = timestamps_ && details_;
    if (slots_[slot_index].stamped) {
      list->ResolveQueryData(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, static_cast<UINT>(slot_index * STAMPS), STAMPS, timestamp_readback_.Get(),
                             slot_index * STAMPS * sizeof(uint64_t));
    }
  }
  void ReadTimes(size_t slot_index) {
    if (!slots_[slot_index].stamped) return;
    if (timestamp_frequency_ == 0) queue_->GetTimestampFrequency(&timestamp_frequency_);
    const D3D12_RANGE range = {.Begin = slot_index * STAMPS * sizeof(uint64_t), .End = (slot_index + 1) * STAMPS * sizeof(uint64_t)};
    uint64_t* mapped = nullptr;
    if (timestamp_frequency_ == 0 || FAILED(timestamp_readback_->Map(0, &range, reinterpret_cast<void**>(&mapped)))) return;
    const uint64_t* stamps = mapped + slot_index * STAMPS;
    const auto ms = [&](uint64_t from, uint64_t to) { return (to > from) ? static_cast<double>(to - from) * 1000.0 / timestamp_frequency_ : 0.0; };
    if (slot_traced_[slot_index]) {
      gpu_ms_[0] = ms(stamps[0], stamps[1]);
      gpu_ms_[1] = ms(stamps[1], stamps[2]);
      gpu_ms_[2] = ms(stamps[2], stamps[3]);
      gpu_ms_[5] = slot_clustered_[slot_index] ? ms(stamps[4], stamps[5]) : 0.0;
      WriteProfile(slot_index, std::format(",{:.3f},{:.3f},{:.3f},{:.3f},,{:.3f},{:.3f},{:.3f},{:.3f},{},{:.3f}", gpu_ms_[0], gpu_ms_[5], gpu_ms_[1],
                                           gpu_ms_[2], ms(stamps[2], stamps[6]), ms(stamps[6], stamps[7]), ms(stamps[7], stamps[8]),
                                           ms(stamps[8], stamps[3]), slots_[slot_index].gpu_bakes, ms(stamps[3], stamps[9])));
      // The Light Bounce's and reflections' times only while they run (not their last).
      for (size_t kind = 0; kind < paths_age_.size(); ++kind) {
        if (++paths_age_[kind] > SLOTS * 2) gpu_ms_[3 + kind] = 0.0;
      }
      if (gpu_ms_[0] + gpu_ms_[1] + gpu_ms_[2] > SLOW_MS && slow_reports_ < SLOW_REPORTS) {
        slow_report_ += std::format("{}[TW ray tracing] A slow request, GPU ms: scene {:.1f}, TLAS {:.1f}, trace {:.1f}; {}", slow_report_.empty() ? "" : "\n", gpu_ms_[0], gpu_ms_[1],
                                   gpu_ms_[2], slots_[slot_index].status);
        ++slow_reports_;
      }
    } else {
      const double paths_ms = ms(stamps[4], stamps[5]);
      WriteProfile(slot_index, std::format(",,,,,{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},,", paths_ms, ms(stamps[4], stamps[6]), ms(stamps[6], stamps[7]),
                                           ms(stamps[7], stamps[8]), ms(stamps[8], stamps[5])));
      gpu_ms_[slot_reflected_[slot_index] ? 4 : 3] = paths_ms;
      paths_age_[slot_reflected_[slot_index] ? 1 : 0] = 0;
      if (paths_ms > SLOW_MS && slow_reports_ < SLOW_REPORTS) {
        slow_report_ += std::format("{}[TW ray tracing] A slow {} request: {:.1f} GPU ms", slow_report_.empty() ? "" : "\n", slot_reflected_[slot_index] ? "reflections" : "Light Bounce",
                                   paths_ms);
        ++slow_reports_;
      }
    }
    const D3D12_RANGE written = {};
    timestamp_readback_->Unmap(0, &written);
  }

  // A mesh's BLAS and micromaps released, its record and pool ranges given back, once the GPU is past this request.
  void ReleaseMesh(Mesh* mesh) {
    ReleaseBlas(&mesh->blas);
    if (mesh->micromaps) releases_.push_back({.value = own_value_ + 1, .resource = std::move(mesh->micromaps)});
    pending_frees_.push_back({.value = own_value_ + 1, .record = mesh->record, .normal_offset = mesh->normal_offset,
                              .triangles = mesh->triangles, .uv_offset = mesh->uv_offset, .first_instance = mesh->first_instance,
                              .instances = static_cast<uint32_t>(mesh->transforms.size())});
  }

  // A place of `bytes` for a static BLAS in `pool` (BUILD_POOL, KEPT_POOL): the first chunk with room, else a new chunk;
  // past BLAS_OWN_BYTES a buffer of its own. Its address is 0 when there is no memory.
  BlasPlace PlaceBlas(uint64_t bytes, uint32_t pool) {
    BlasPlace place;
    if (bytes > BLAS_OWN_BYTES) {
      place.own = Buffer(bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
      if (place.own) place.address = place.own->GetGPUVirtualAddress();
      return place;
    }
    const auto units = static_cast<uint32_t>((bytes + D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1)
                                             / D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
    std::vector<BlasChunk>& chunks = blas_pools_[pool];
    uint32_t chunk = 0;
    while (chunk < chunks.size() && !(chunks[chunk].buffer && chunks[chunk].free.Take(units, &place.offset))) ++chunk;
    if (chunk == chunks.size()) {
      // A new chunk, in the place of one let go if there is one.
      chunk = static_cast<uint32_t>(std::ranges::find_if(chunks, [](const BlasChunk& other) { return !other.buffer; }) - chunks.begin());
      if (chunk == chunks.size()) chunks.emplace_back();
      chunks[chunk].buffer = Buffer(BLAS_CHUNK_BYTES, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                    D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
      if (!chunks[chunk].buffer) return place;
      chunks[chunk].free.free = {{units, BLAS_CHUNK_UNITS - units}};
      place.offset = 0;
    }
    place.pool = pool;
    place.chunk = chunk;
    place.units = units;
    place.address = chunks[chunk].buffer->GetGPUVirtualAddress() + (uint64_t{place.offset} * D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
    return place;
  }

  // A static BLAS's place given back once the GPU is past this request.
  void ReleaseBlas(BlasPlace* place) {
    if (place->own) {
      releases_.push_back({.value = own_value_ + 1, .resource = std::move(place->own)});
    } else if (place->units != 0) {
      releases_.push_back({.value = own_value_ + 1, .pool = place->pool, .chunk = place->chunk, .offset = place->offset, .units = place->units});
    }
    *place = {};
  }

  // The leaves' micromaps on their way: read once the GPU is past their request (their readbacks released), baked one at
  // a time on a thread, then waiting for a request to build them (Record).
  // The bakes on their way on the workers.
  size_t MicromapsBaking() const {
    return static_cast<size_t>(std::ranges::count_if(micromap_baking_, [](const auto& baking) { return baking.valid(); }));
  }

  void AdvanceMicromaps(uint64_t completed) {
    // A removed device's fence reads all ones: nothing finished, the bakes on their way are no failures.
    if (completed == UINT64_MAX) return;
    for (std::unique_ptr<MicromapBake>& read : micromap_reads_) {
      if (read->value > completed) continue;
#if defined(RENODX_OMM_SDK)
      // GPU Micromap Baking: its histogram read back (the SDK's usage counts: a count, a level and a format each; the
      // zero ones left out), the rest stays on the GPU.
      if (read->gpu) {
        const D3D12_RANGE range = {.Begin = read->histogram_offset, .End = read->histogram_offset + read->histogram_bytes};
        const uint8_t* mapped = nullptr;
        if (!read->gpu_failed && read->gpu_readback
            && SUCCEEDED(read->gpu_readback->Map(0, &range, reinterpret_cast<void**>(const_cast<uint8_t**>(&mapped))))) {
          const auto* counts = reinterpret_cast<const ommCpuOpacityMicromapUsageCount*>(mapped + range.Begin);
          for (size_t entry = 0; entry < read->histogram_bytes / sizeof(ommCpuOpacityMicromapUsageCount); ++entry) {
            if (counts[entry].count == 0) continue;
            read->histogram.push_back({.Count = counts[entry].count, .SubdivisionLevel = counts[entry].subdivisionLevel,
                                       .Format = static_cast<D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT>(counts[entry].format)});
            // Their data's bytes: 4^level micro-triangles, 1 or 2 bits each.
            micromap_tally_[1] += uint64_t{counts[entry].count}
                                  * ((((uint64_t{1} << (2u * counts[entry].subdivisionLevel)) * counts[entry].format) + 7) / 8);
          }
          const D3D12_RANGE none = {};
          read->gpu_readback->Unmap(0, &none);
          read->gpu_readback.Reset();
#if defined(RENODX_RECORDER)
          // The recorder's journal: the bake's data, descs and indices as a CPU bake's (their sizes the post-dispatch
          // info's; none when they do not fit their room: the snapshot refuses its scene).
          const uint8_t* journal = nullptr;
          if (read->journal_readback && SUCCEEDED(read->journal_readback->Map(0, nullptr, reinterpret_cast<void**>(const_cast<uint8_t**>(&journal))))) {
            ommGpuPostDispatchInfo info = {};
            std::memcpy(&info, journal + read->journal_offsets[0], sizeof(info));
            if (info.outOmmArraySizeInBytes <= read->journal_limits[0] && info.outOmmDescSizeInBytes <= read->journal_limits[1]) {
              read->data.assign(journal + read->journal_offsets[1], journal + read->journal_offsets[1] + info.outOmmArraySizeInBytes);
              const auto* descs = reinterpret_cast<const D3D12_RAYTRACING_OPACITY_MICROMAP_DESC*>(journal + read->journal_offsets[2]);
              read->descs.assign(descs, descs + (info.outOmmDescSizeInBytes / sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_DESC)));
              const auto* indices = reinterpret_cast<const uint32_t*>(journal + read->journal_offsets[3]);
              read->indices.assign(indices, indices + read->triangles);
            }
            read->journal_readback->Unmap(0, &none);
          }
          read->journal_readback.Reset();
#endif
          ++micromap_tally_[0];
          micromaps_baked_.push_back(std::move(read));
        } else {
          ++micromap_tally_[2];
          read.reset();
        }
        continue;
      }
#endif
      // Its places in its request's read-backs (Map points at a buffer's start).
      const D3D12_RANGE uv_range = {.Begin = read->uvs_offset, .End = read->uvs_offset + (uint64_t{read->triangles} * 12)};
      const D3D12_RANGE mask_range = {.Begin = read->mask_offset,
                                      .End = read->mask_offset + (uint64_t{dlaa_bridge::RT_ATLAS_SIZE} * dlaa_bridge::RT_ATLAS_SIZE)};
      const D3D12_RANGE written = {};
      const uint8_t* uvs = nullptr;
      const uint8_t* mask = nullptr;
      if (SUCCEEDED(read->uvs->Map(0, &uv_range, reinterpret_cast<void**>(const_cast<uint8_t**>(&uvs))))) {
        const auto* first = reinterpret_cast<const uint32_t*>(uvs + uv_range.Begin);
        read->uv_data.assign(first, first + (uint64_t{read->triangles} * 3));
        read->uvs->Unmap(0, &written);
      }
      if (SUCCEEDED(read->mask->Map(0, &mask_range, reinterpret_cast<void**>(const_cast<uint8_t**>(&mask))))) {
        read->mask_data.assign(mask + mask_range.Begin, mask + mask_range.End);
        read->mask->Unmap(0, &written);
      }
      read->uvs.Reset();
      read->mask.Reset();
      if (!read->uv_data.empty() && !read->mask_data.empty()) micromap_queue_.push_back(std::move(read));
      read.reset();
    }
    std::erase(micromap_reads_, nullptr);
    // MICROMAP_WORKERS bakes at once, each with its baker (one at a time, a battle's new grass sets waited seconds for
    // theirs: without, dense grass met MAX_CANDIDATES and stayed dark, ATN 2026-10-29); grass sets first.
    for (size_t worker = 0; worker < MICROMAP_WORKERS; ++worker) {
      auto& baking = micromap_baking_[worker];
#if defined(RENODX_RECORDER)
      if (Lockstep() && baking.valid()) baking.wait();
#endif
      if (baking.valid() && baking.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        std::unique_ptr<MicromapBake> baked = baking.get();
        ++micromap_tally_[baked->indices.empty() ? 2 : 0];
        micromap_tally_[1] += baked->data.size();
        if (!baked->indices.empty()) micromaps_baked_.push_back(std::move(baked));
      }
#if defined(RENODX_OMM_SDK)
      if (!baking.valid() && !micromap_queue_.empty()) {
        const auto next = std::ranges::find_if(micromap_queue_, [](const auto& queued) { return queued->set; });
        const auto taken = (next != micromap_queue_.end()) ? next : micromap_queue_.begin();
        baking = std::async(std::launch::async, [this, baker = omm_.bakers[worker], bake = std::move(*taken)]() mutable {
          BakeMicromaps(bake.get(), baker);
          return std::move(bake);
        });
        micromap_queue_.erase(taken);
      }
#endif
    }
    // The bakes' tally at most every 10 s (the log).
    if (details_ && (micromap_tally_[0] + micromap_tally_[2]) != 0 && GetTickCount64() >= micromap_logged_ + 10000) {
      micromap_logged_ = GetTickCount64();
      slow_report_ += std::format("{}[TW ray tracing] Leaf micromaps: {} meshes baked ({} KB), {} failed; {} grass sets given theirs.",
                                  (slow_report_.empty() ? "" : "\n"), micromap_tally_[0], micromap_tally_[1] >> 10, micromap_tally_[2],
                                  micromap_tally_[3]);
      micromap_tally_ = {};
    }
  }

#if defined(RENODX_OMM_SDK)
  // GPU Micromap Baking: the OMM SDK's compute pipelines as D3D12 ones (each a table of its descriptor ranges, the global
  // constants' CBV, the dispatches' own root constants, the SDK's static samplers), the descriptor ring, the indirect
  // dispatch's signature, the corners' indices and the SDK's static buffers; false: it stays off (the CPU bakes go on),
  // `why` says what failed (the status line).
  bool MakeGpuBaker(std::string* why) {
    const ommGpuPipelineInfoDesc* info = nullptr;
    if (omm_.gpu_pipeline == nullptr) {
      *why = "no GPU baker";
      return false;
    }
    if (omm_.gpu_pipeline_desc(omm_.gpu_pipeline, &info) != ommResult_SUCCESS) {
      *why = "no pipeline descriptions";
      return false;
    }
    std::vector<D3D12_STATIC_SAMPLER_DESC> samplers;
    for (uint32_t index = 0; index < info->staticSamplersNum; ++index) {
      const ommGpuStaticSamplerDesc& sampler = info->staticSamplers[index];
      D3D12_TEXTURE_ADDRESS_MODE address = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
      switch (sampler.desc.addressingMode) {
        case ommTextureAddressMode_Mirror:     address = D3D12_TEXTURE_ADDRESS_MODE_MIRROR; break;
        case ommTextureAddressMode_Clamp:      address = D3D12_TEXTURE_ADDRESS_MODE_CLAMP; break;
        case ommTextureAddressMode_Border:     address = D3D12_TEXTURE_ADDRESS_MODE_BORDER; break;
        case ommTextureAddressMode_MirrorOnce: address = D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE; break;
        default:                               break;
      }
      samplers.push_back({.Filter = (sampler.desc.filter == ommTextureFilterMode_Nearest) ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR,
                          .AddressU = address,
                          .AddressV = address,
                          .AddressW = address,
                          .MaxAnisotropy = 1,
                          .ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER,
                          .BorderColor = (sampler.desc.borderAlpha > 0.5f) ? D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK
                                                                           : D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
                          .MaxLOD = D3D12_FLOAT32_MAX,
                          .ShaderRegister = sampler.registerIndex});
    }
    auto& gpu = omm_gpu_;
    gpu.pipelines.resize(info->pipelineNum);
    for (uint32_t index = 0; index < info->pipelineNum; ++index) {
      // The raster ones are not needed: the bakes run ComputeOnly.
      if (info->pipelines[index].type != ommGpuPipelineType_Compute) continue;
      const ommGpuComputePipelineDesc& compute = info->pipelines[index].compute;
      std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
      for (uint32_t range = 0; range < compute.descriptorRangeNum; ++range) {
        const ommGpuDescriptorRangeDesc& desc = compute.descriptorRanges[range];
        ranges.push_back({.RangeType = (desc.descriptorType == ommGpuDescriptorType_RawBufferWrite) ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV
                                                                                                    : D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                          .NumDescriptors = desc.descriptorNum,
                          .BaseShaderRegister = desc.baseRegisterIndex,
                          .OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND});
      }
      OmmGpu::Pipeline& made = gpu.pipelines[index];
      std::vector<D3D12_ROOT_PARAMETER> parameters;
      if (!ranges.empty()) {
        made.table = static_cast<int>(parameters.size());
        parameters.push_back({.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
                              .DescriptorTable = {.NumDescriptorRanges = static_cast<UINT>(ranges.size()), .pDescriptorRanges = ranges.data()}});
      }
      // The global constants for every pipeline (omm_rasterize_cs_r reads them without hasConstantData).
      gpu.globals_bytes = info->globalConstantBufferDesc.maxDataSize;
      if (info->globalConstantBufferDesc.maxDataSize != 0) {
        made.global = static_cast<int>(parameters.size());
        parameters.push_back({.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
                              .Descriptor = {.ShaderRegister = info->globalConstantBufferDesc.registerIndex}});
      }
      if (info->localConstantBufferDesc.maxDataSize >= 4) {
        made.constants = static_cast<int>(parameters.size());
        made.constant_count = info->localConstantBufferDesc.maxDataSize / 4;
        parameters.push_back({.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
                              .Constants = {.ShaderRegister = info->localConstantBufferDesc.registerIndex, .Num32BitValues = made.constant_count}});
      }
      const D3D12_ROOT_SIGNATURE_DESC root_desc = {.NumParameters = static_cast<UINT>(parameters.size()),
                                                   .pParameters = parameters.data(),
                                                   .NumStaticSamplers = static_cast<UINT>(samplers.size()),
                                                   .pStaticSamplers = samplers.data()};
      ComPtr<ID3DBlob> serialized;
      ComPtr<ID3DBlob> errors;
      if (FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, serialized.GetAddressOf(), errors.GetAddressOf()))
          || FAILED(device_->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(made.root.GetAddressOf())))) {
        *why = std::format("pipeline {} ({}): root signature {}", index, compute.shaderFileName != nullptr ? compute.shaderFileName : "?",
                           errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : std::string());
        return false;
      }
      const D3D12_COMPUTE_PIPELINE_STATE_DESC state_desc = {
          .pRootSignature = made.root.Get(), .CS = {.pShaderBytecode = compute.computeShader.data, .BytecodeLength = compute.computeShader.size}};
      if (const HRESULT result = device_->CreateComputePipelineState(&state_desc, IID_PPV_ARGS(made.state.GetAddressOf())); FAILED(result)) {
        *why = std::format("pipeline {} ({}): state 0x{:08X} ({} ranges, global b{} {}, local b{} {} bytes)", index,
                           compute.shaderFileName != nullptr ? compute.shaderFileName : "?", static_cast<uint32_t>(result), ranges.size(),
                           info->globalConstantBufferDesc.registerIndex, compute.hasConstantData ? 1 : 0,
                           info->localConstantBufferDesc.registerIndex, info->localConstantBufferDesc.maxDataSize);
        return false;
      }
    }
    const D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, .NumDescriptors = OMM_DESCRIPTORS,
                                                  .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    const D3D12_INDIRECT_ARGUMENT_DESC argument = {.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH};
    const D3D12_COMMAND_SIGNATURE_DESC signature = {.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS), .NumArgumentDescs = 1, .pArgumentDescs = &argument};
    gpu.indices = Buffer(uint64_t{MICROMAP_TRIANGLES} * 12, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    uint32_t* corners = nullptr;
    if (FAILED(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(gpu.heap.GetAddressOf())))
        || FAILED(device_->CreateCommandSignature(&signature, nullptr, IID_PPV_ARGS(gpu.dispatch.GetAddressOf()))) || !gpu.indices
        || FAILED(gpu.indices->Map(0, nullptr, reinterpret_cast<void**>(&corners)))) {
      *why = "its descriptors, signature or indices";
      return false;
    }
    std::iota(corners, corners + (uint64_t{MICROMAP_TRIANGLES} * 3), 0u);
    gpu.indices->Unmap(0, nullptr);
    for (size_t kind = 0; kind < gpu.statics.size(); ++kind) {
      const ommGpuResourceType type = (kind == 0) ? ommGpuResourceType_STATIC_VERTEX_BUFFER : ommGpuResourceType_STATIC_INDEX_BUFFER;
      size_t bytes = 0;
      if (omm_.gpu_static_data(type, nullptr, &bytes) != ommResult_SUCCESS || bytes == 0) continue;
      gpu.statics[kind] = Buffer(Align(bytes, 256), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
      uint8_t* data = nullptr;
      if (!gpu.statics[kind] || FAILED(gpu.statics[kind]->Map(0, nullptr, reinterpret_cast<void**>(&data)))) {
        *why = "its static buffers";
        return false;
      }
      omm_.gpu_static_data(type, data, &bytes);
      gpu.statics[kind]->Unmap(0, nullptr);
    }
    return true;
  }

  // GPU Micromap Baking at the end of a request: `bakes` (leaves' and grass sets' waiting: their UVs and masks copied when
  // read) baked by the OMM SDK's GPU baker (ComputeOnly, 4-state as the ray queries test them, the CPU bakes'
  // settings): the UVs copied with garbage coordinates zeroed (rt_omm_uvs), then the bakes' dispatch chains stage by stage (all bakes' step k, then a UAV barrier; every resource
  // in the state its descriptor needs), into outputs and transient pools the request's bakes share (256-byte aligned
  // places of their own); the histograms copied to a
  // read-back (AdvanceMicromaps: the bakes' builds wait for them, the rest stays on the GPU). A bake the SDK refuses keeps
  // its leaves alpha-tested.
  void BakeMicromapsOnGpu(ID3D12GraphicsCommandList4* list, const std::vector<MicromapBake*>& bakes) {
    if (bakes.empty()) return;
    auto& gpu = omm_gpu_;
    // A dispatch of a bake's chain, copied.
    struct Step {
      const wchar_t* name = nullptr;
      bool indirect = false;
      uint16_t pipeline_index = 0;
      std::vector<ommGpuResource> resources;
      std::vector<uint8_t> constants;
      uint32_t grid_width = 0;
      uint32_t grid_height = 0;
      ommGpuResource arguments = {};
      uint64_t arguments_offset = 0;
    };
    struct Planned {
      MicromapBake* bake = nullptr;
      ommGpuDispatchConfigDesc config = {};
      ommGpuPreDispatchInfo info = {};
      // The clamped UVs', then the outputs' (array data, descs, desc histogram, indices, index histogram, post-dispatch
      // info): places and sizes; its transient pools' places in the request's (one buffer each).
      std::array<uint64_t, 7> offsets = {};
      std::array<uint64_t, 7> sizes = {};
      std::array<uint64_t, OMM_MAX_TRANSIENT_POOL_BUFFERS> transient_offsets = {};
      std::array<uint64_t, OMM_MAX_TRANSIENT_POOL_BUFFERS> transient_sizes = {};
      std::vector<Step> steps;
    };
    std::vector<Planned> planned;
    std::string why;  // the last failure (logged once a request)
    std::array<uint64_t, 7> totals = {};
    std::array<uint64_t, OMM_MAX_TRANSIENT_POOL_BUFFERS> transient_bytes = {};
    for (MicromapBake* bake : bakes) {
      Planned plan = {.bake = bake, .config = ommGpuDispatchConfigDescDefault(), .info = ommGpuPreDispatchInfoDefault()};
      ommGpuDispatchConfigDesc& config = plan.config;
      config.bakeFlags = ommGpuBakeFlags_PerformSetupAndBake | ommGpuBakeFlags_ComputeOnly | ommGpuBakeFlags_Force32BitIndices
                         | ommGpuBakeFlags_DisableTexCoordDeduplication;
      config.runtimeSamplerDesc = {.addressingMode = ommTextureAddressMode_Wrap, .filter = ommTextureFilterMode_Linear, .borderAlpha = 0.f};
      config.alphaMode = ommAlphaMode_Test;
      config.alphaTextureWidth = dlaa_bridge::RT_ATLAS_SIZE;
      config.alphaTextureHeight = dlaa_bridge::RT_ATLAS_SIZE;
      config.alphaTextureChannel = 0;
      config.texCoordFormat = ommTexCoordFormat_UV16_FLOAT;
      config.texCoordOffsetInBytes = static_cast<uint32_t>(totals[0]);
      config.indexFormat = ommIndexFormat_UINT_32;
      config.indexCount = bake->triangles * 3;
      config.alphaCutoff = bake->cutoff;
      config.maxSubdivisionLevel = static_cast<uint8_t>(bake->grass ? grass_micromap_level_ : leaf_micromap_level_);
      config.maxOutOmmArraySize = MICROMAP_MESH_BYTES;
      config.maxScratchMemorySize = ommGpuScratchMemoryBudget_MB_64;
      if (const ommResult result = omm_.gpu_pre_dispatch(omm_.gpu_pipeline, &config, &plan.info); result != ommResult_SUCCESS) {
        bake->gpu_failed = true;
        why = std::format("pre-dispatch info {}", static_cast<int>(result));
        continue;
      }
      plan.sizes = {uint64_t{bake->triangles} * 12,
                    plan.info.outOmmArraySizeInBytes,
                    plan.info.outOmmDescSizeInBytes,
                    plan.info.outOmmArrayHistogramSizeInBytes,
                    plan.info.outOmmIndexBufferSizeInBytes,
                    plan.info.outOmmIndexHistogramSizeInBytes,
                    plan.info.outOmmPostDispatchInfoSizeInBytes};
      for (size_t kind = 0; kind < totals.size(); ++kind) {
        plan.offsets[kind] = totals[kind];
        totals[kind] = Align(totals[kind] + std::max<uint64_t>(plan.sizes[kind], 4), 256);
      }
      for (uint32_t pool = 0; pool < plan.info.numTransientPoolBuffers && pool < OMM_MAX_TRANSIENT_POOL_BUFFERS; ++pool) {
        plan.transient_offsets[pool] = transient_bytes[pool];
        plan.transient_sizes[pool] = std::max<uint64_t>(plan.info.transientPoolBufferSizeInBytes[pool], 4);
        transient_bytes[pool] = Align(transient_bytes[pool] + plan.transient_sizes[pool], 256);
      }
      planned.push_back(plan);
    }
    const auto fail_all = [&] {
      for (Planned& plan : planned) plan.bake->gpu_failed = true;
    };
    if (planned.empty()) return;
    // The clamped UVs and the scratch (kept, grown: the SDK's transient pools "can be reused after baking", omm.h), the
    // request's outputs and the histograms' read-back.
    bool made = GrowBuffer(std::addressof(gpu.uvs), totals[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    for (size_t pool = 0; pool < transient_bytes.size() && made; ++pool) {
      if (transient_bytes[pool] != 0) made = GrowBuffer(std::addressof(gpu.transient[pool]), transient_bytes[pool], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    std::array<ComPtr<ID3D12Resource>, 6> outputs;
    for (size_t kind = 0; kind < outputs.size() && made; ++kind) {
      outputs[kind] = Buffer(totals[kind + 1], D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      made = static_cast<bool>(outputs[kind]);
    }
    ComPtr<ID3D12Resource> readback;
    if (made) readback = Buffer(totals[3], D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    // The bakes' global constants: one upload buffer, a place each.
    const uint64_t globals_stride = Align(std::max(gpu.globals_bytes, 4u), 256);
    ComPtr<ID3D12Resource> globals;
    uint8_t* globals_data = nullptr;
    if (made) globals = Buffer(globals_stride * planned.size(), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    if (!made || !readback || !globals || FAILED(globals->Map(0, nullptr, reinterpret_cast<void**>(&globals_data)))) {
      fail_all();
      slow_report_ += std::format("{}[TW ray tracing] GPU micromap bakes: their buffers failed ({} bakes, {} KB).", slow_report_.empty() ? "" : "\n",
                                  planned.size(), (totals[0] + totals[1] + totals[2] + totals[3] + totals[4] + totals[5] + totals[6]) >> 10);
      return;
    }
    Mark(list, L"micromap bakes");
    const D3D12_RESOURCE_BARRIER uav_barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV};
    // The UVs clamped (the main root signature: rt_omm_uvs reads t0, the read's copy, writes u6, its count at b1).
    std::vector<D3D12_RESOURCE_BARRIER> barriers = {
        Transition(gpu.uvs.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};
    list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    list->SetComputeRootSignature(root_signature_.Get());
    list->SetPipelineState(gpu.clamp.Get());
    for (const Planned& plan : planned) {
      const std::array<uint32_t, 8> constants = {plan.bake->triangles};
      list->SetComputeRootShaderResourceView(ROOT_SCENE, plan.bake->uvs->GetGPUVirtualAddress() + plan.bake->uvs_offset);
      list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_DATA, gpu.uvs->GetGPUVirtualAddress() + plan.offsets[0]);
      list->SetComputeRoot32BitConstants(ROOT_DISPATCH, static_cast<UINT>(constants.size()), constants.data(), 0);
      list->Dispatch((plan.bake->triangles + 63) / 64, 1, 1);
    }
    const auto clamped = Transition(gpu.uvs.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    list->ResourceBarrier(1, &clamped);
    // The SDK's chains (its chain memory is its own again at its next call: each step copied), then recorded stage by
    // stage: every bake's step k together (their resources' states in one barrier, their dispatches, one UAV barrier),
    // as their outputs and transient pools are places of their own (bake after bake, a barrier after each of ~11 small
    // dispatches, left the GPU mostly idle). A step whose states clash with another's in its stage waits for a wave of
    // its own.
    ID3D12DescriptorHeap* heaps[] = {gpu.heap.Get()};
    list->SetDescriptorHeaps(1, heaps);
    size_t stages = 0;
    for (size_t index = 0; index < planned.size(); ++index) {
      Planned& plan = planned[index];
      const ommGpuDispatchChain* chain = nullptr;
      if (const ommResult result = omm_.gpu_dispatch(omm_.gpu_pipeline, &plan.config, &chain); result != ommResult_SUCCESS || chain == nullptr) {
        plan.bake->gpu_failed = true;
        why = std::format("dispatch {}", static_cast<int>(result));
        continue;
      }
      if (chain->globalCBufferDataSize > globals_stride) {
        plan.bake->gpu_failed = true;
        why = std::format("its constants ({} bytes)", chain->globalCBufferDataSize);
        continue;
      }
      std::memcpy(globals_data + (index * globals_stride), chain->globalCBufferData, chain->globalCBufferDataSize);
      for (uint32_t step = 0; step < chain->numDispatches; ++step) {
        const ommGpuDispatchDesc& dispatch = chain->dispatches[step];
        if (dispatch.type == ommGpuDispatchType_BeginLabel || dispatch.type == ommGpuDispatchType_EndLabel) continue;
        // The SDK's names are its own again at its next call: kept for the markers (LostReport).
        static std::unordered_set<std::wstring> step_names;
        const auto step_name = [](const char* name) {
          return step_names.emplace(name, name + std::strlen(name)).first->c_str();
        };
        if (dispatch.type == ommGpuDispatchType_ComputeIndirect) {
          const ommGpuComputeIndirectDesc& indirect = dispatch.computeIndirect;
          plan.steps.push_back({.name = step_name(indirect.name), .indirect = true, .pipeline_index = indirect.pipelineIndex,
                                .resources = {indirect.resources, indirect.resources + indirect.resourceNum},
                                .constants = {indirect.localConstantBufferData, indirect.localConstantBufferData + indirect.localConstantBufferDataSize},
                                .arguments = indirect.indirectArg, .arguments_offset = indirect.indirectArgByteOffset});
        } else if (dispatch.type == ommGpuDispatchType_Compute) {
          const ommGpuComputeDesc& compute = dispatch.compute;
          plan.steps.push_back({.name = step_name(compute.name), .pipeline_index = compute.pipelineIndex, .resources = {compute.resources, compute.resources + compute.resourceNum},
                                .constants = {compute.localConstantBufferData, compute.localConstantBufferData + compute.localConstantBufferDataSize},
                                .grid_width = compute.gridWidth, .grid_height = compute.gridHeight});
        } else {
          plan.bake->gpu_failed = true;
          why = std::format("dispatch type {}", static_cast<int>(dispatch.type));
          plan.steps.clear();
          break;
        }
      }
      stages = std::max(stages, plan.steps.size());
    }
    std::unordered_map<ID3D12Resource*, D3D12_RESOURCE_STATES> states;  // the tracked ones' states now (upload buffers stay readable)
    for (const auto& output : outputs) states[output.Get()] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    for (const auto& pool : gpu.transient) {
      if (pool) states[pool.Get()] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    for (const Planned& plan : planned) states[gpu.masks[plan.bake->mask_index].Get()] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    states[gpu.uvs.Get()] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    const D3D12_CPU_DESCRIPTOR_HANDLE cpu_start = gpu.heap->GetCPUDescriptorHandleForHeapStart();
    const D3D12_GPU_DESCRIPTOR_HANDLE gpu_start = gpu.heap->GetGPUDescriptorHandleForHeapStart();
    const auto state_for = [&](ID3D12Resource* resource, D3D12_RESOURCE_STATES needed) {
      const auto tracked = states.find(resource);
      if (tracked == states.end() || tracked->second == needed) return;
      barriers.push_back(Transition(resource, tracked->second, needed));
      tracked->second = needed;
    };
    // A resource a chain names: its buffer (or the mask's texture), the bake's place and size there.
    const auto locate = [&](size_t index, const ommGpuResource& resource, uint64_t* offset, uint64_t* bytes) -> ID3D12Resource* {
      const Planned& plan = planned[index];
      const auto output = [&](size_t kind) {
        *offset = plan.offsets[kind + 1];
        *bytes = std::max<uint64_t>(plan.sizes[kind + 1], 4);
        return outputs[kind].Get();
      };
      const auto whole = [&](const ComPtr<ID3D12Resource>& buffer) -> ID3D12Resource* {
        *offset = 0;
        *bytes = buffer ? buffer->GetDesc().Width : 0;
        return buffer.Get();
      };
      switch (resource.type) {
        case ommGpuResourceType_IN_ALPHA_TEXTURE:             return gpu.masks[plan.bake->mask_index].Get();
        case ommGpuResourceType_IN_TEXCOORD_BUFFER:           return whole(gpu.uvs);
        case ommGpuResourceType_IN_INDEX_BUFFER:              return whole(gpu.indices);
        case ommGpuResourceType_OUT_OMM_ARRAY_DATA:           return output(0);
        case ommGpuResourceType_OUT_OMM_DESC_ARRAY:           return output(1);
        case ommGpuResourceType_OUT_OMM_DESC_ARRAY_HISTOGRAM: return output(2);
        case ommGpuResourceType_OUT_OMM_INDEX_BUFFER:         return output(3);
        case ommGpuResourceType_OUT_OMM_INDEX_HISTOGRAM:      return output(4);
        case ommGpuResourceType_OUT_POST_DISPATCH_INFO:       return output(5);
        case ommGpuResourceType_TRANSIENT_POOL_BUFFER:
          if (resource.indexInPool >= gpu.transient.size()) return nullptr;
          *offset = plan.transient_offsets[resource.indexInPool];
          *bytes = plan.transient_sizes[resource.indexInPool];
          return gpu.transient[resource.indexInPool].Get();
        case ommGpuResourceType_STATIC_VERTEX_BUFFER: return whole(gpu.statics[0]);
        case ommGpuResourceType_STATIC_INDEX_BUFFER:  return whole(gpu.statics[1]);
        default:                                      return nullptr;
      }
    };
    for (size_t stage = 0; stage < stages; ++stage) {
      std::vector<size_t> waiting;
      for (size_t index = 0; index < planned.size(); ++index) {
        if (!planned[index].bake->gpu_failed && stage < planned[index].steps.size()) waiting.push_back(index);
      }
      while (!waiting.empty()) {
        // A wave: the waiting steps whose states agree, their descriptors written, then their barrier and dispatches.
        struct Ready {
          size_t index = 0;
          UINT table = 0;
          ID3D12Resource* arguments = nullptr;
          uint64_t arguments_offset = 0;
        };
        struct Located {
          ID3D12Resource* target = nullptr;
          uint64_t offset = 0;
          uint64_t bytes = 0;
          D3D12_RESOURCE_STATES needed = D3D12_RESOURCE_STATE_COMMON;
        };
        std::unordered_map<ID3D12Resource*, D3D12_RESOURCE_STATES> wave_states;
        std::vector<Ready> ready;
        std::vector<size_t> later;
        for (const size_t index : waiting) {
          Planned& plan = planned[index];
          const Step& step = plan.steps[stage];
          if (step.pipeline_index >= gpu.pipelines.size() || !gpu.pipelines[step.pipeline_index].state) {
            plan.bake->gpu_failed = true;
            why = std::format("pipeline {}", step.pipeline_index);
            continue;
          }
          std::vector<Located> located;
          for (const ommGpuResource& resource : step.resources) {
            Located place = {.needed = (resource.stateNeeded == ommGpuDescriptorType_RawBufferWrite) ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
                                                                                                     : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
            place.target = locate(index, resource, &place.offset, &place.bytes);
            if (place.target == nullptr) {
              plan.bake->gpu_failed = true;
              why = std::format("resource type {} ({})", static_cast<int>(resource.type), resource.indexInPool);
              break;
            }
            located.push_back(place);
          }
          Located arguments = {.needed = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT};
          if (step.indirect && !plan.bake->gpu_failed) {
            arguments.target = locate(index, step.arguments, &arguments.offset, &arguments.bytes);
            if (arguments.target == nullptr) {
              plan.bake->gpu_failed = true;
              why = std::format("indirect arguments' type {}", static_cast<int>(step.arguments.type));
            }
            arguments.offset += step.arguments_offset;
          }
          if (plan.bake->gpu_failed) continue;
          const auto clashes = [&](const Located& place) {
            const auto found = wave_states.find(place.target);
            return found != wave_states.end() && found->second != place.needed;
          };
          if (std::ranges::any_of(located, clashes) || (arguments.target != nullptr && clashes(arguments))) {
            later.push_back(index);
            continue;
          }
          const UINT resource_count = static_cast<UINT>(located.size());
          if (gpu.next + resource_count > OMM_DESCRIPTORS) gpu.next = 0;
          for (UINT number = 0; number < resource_count; ++number) {
            const auto& [target, offset, bytes, needed] = located[number];
            const D3D12_CPU_DESCRIPTOR_HANDLE handle = {.ptr = cpu_start.ptr + (uint64_t{gpu.next + number} * descriptor_size_)};
            const ommGpuDescriptorType type = step.resources[number].stateNeeded;
            if (type == ommGpuDescriptorType_TextureRead) {
              D3D12_SHADER_RESOURCE_VIEW_DESC view = {.Format = DXGI_FORMAT_R8_UNORM, .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                                                      .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING};
              view.Texture2D = {.MostDetailedMip = step.resources[number].mipOffset, .MipLevels = std::max<UINT>(step.resources[number].mipNum, 1u)};
              device_->CreateShaderResourceView(target, &view, handle);
            } else if (type == ommGpuDescriptorType_RawBufferWrite) {
              D3D12_UNORDERED_ACCESS_VIEW_DESC view = {.Format = DXGI_FORMAT_R32_TYPELESS, .ViewDimension = D3D12_UAV_DIMENSION_BUFFER};
              view.Buffer = {.FirstElement = offset / 4, .NumElements = static_cast<UINT>(bytes / 4), .Flags = D3D12_BUFFER_UAV_FLAG_RAW};
              device_->CreateUnorderedAccessView(target, nullptr, &view, handle);
            } else {
              const bool raw = type == ommGpuDescriptorType_RawBufferRead;
              D3D12_SHADER_RESOURCE_VIEW_DESC view = {.Format = raw ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_R32_UINT,
                                                      .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                                                      .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING};
              view.Buffer = {.FirstElement = offset / 4, .NumElements = static_cast<UINT>(bytes / 4),
                             .Flags = raw ? D3D12_BUFFER_SRV_FLAG_RAW : D3D12_BUFFER_SRV_FLAG_NONE};
              device_->CreateShaderResourceView(target, &view, handle);
            }
            wave_states[target] = needed;
          }
          if (arguments.target != nullptr) wave_states[arguments.target] = arguments.needed;
          ready.push_back({.index = index, .table = gpu.next, .arguments = arguments.target, .arguments_offset = arguments.offset});
          gpu.next += resource_count;
        }
        if (!ready.empty()) Mark(list, planned[ready.front().index].steps[stage].name);
        barriers.clear();
        for (const auto& [resource, needed] : wave_states) state_for(resource, needed);
        if (!barriers.empty()) list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
        for (const Ready& entry : ready) {
          const Step& step = planned[entry.index].steps[stage];
          const OmmGpu::Pipeline& pipeline = gpu.pipelines[step.pipeline_index];
          list->SetComputeRootSignature(pipeline.root.Get());
          list->SetPipelineState(pipeline.state.Get());
          if (pipeline.table >= 0) list->SetComputeRootDescriptorTable(pipeline.table, {.ptr = gpu_start.ptr + (uint64_t{entry.table} * descriptor_size_)});
          if (pipeline.global >= 0) {
            list->SetComputeRootConstantBufferView(pipeline.global, globals->GetGPUVirtualAddress() + (entry.index * globals_stride));
          }
          if (pipeline.constants >= 0 && step.constants.size() >= 4) {
            list->SetComputeRoot32BitConstants(pipeline.constants, std::min(static_cast<UINT>(step.constants.size() / 4), pipeline.constant_count),
                                               step.constants.data(), 0);
          }
          if (step.indirect) {
            list->ExecuteIndirect(gpu.dispatch.Get(), 1, entry.arguments, entry.arguments_offset, nullptr, 0);
          } else {
            list->Dispatch(step.grid_width, step.grid_height, 1);
          }
        }
        if (!ready.empty()) list->ResourceBarrier(1, &uav_barrier);
        waiting = std::move(later);
      }
    }
    // The collapsed triangles' micromaps unknown opaque (rt_omm_uvs: the rays' alpha test decides there).
    barriers.clear();
    state_for(outputs[3].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    state_for(gpu.uvs.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (!barriers.empty()) list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    list->SetComputeRootSignature(root_signature_.Get());
    list->SetPipelineState(gpu.clamp.Get());
    for (const Planned& plan : planned) {
      if (plan.bake->gpu_failed) continue;
      const std::array<uint32_t, 8> constants = {plan.bake->triangles, 1u};
      list->SetComputeRootShaderResourceView(ROOT_SCENE, gpu.uvs->GetGPUVirtualAddress() + plan.offsets[0]);
      list->SetComputeRootUnorderedAccessView(ROOT_MICROMAP_DATA, outputs[3]->GetGPUVirtualAddress() + plan.offsets[4]);
      list->SetComputeRoot32BitConstants(ROOT_DISPATCH, static_cast<UINT>(constants.size()), constants.data(), 0);
      list->Dispatch((plan.bake->triangles + 63) / 64, 1, 1);
    }
    list->ResourceBarrier(1, &uav_barrier);
    // What the builds read (array data, descs, indices) readable from now on, the histograms copied to the read-back, the
    // kept ones back to their resting states.
    barriers.clear();
    for (const size_t kind : {0u, 1u, 3u}) {
      if (states[outputs[kind].Get()] != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        barriers.push_back(Transition(outputs[kind].Get(), states[outputs[kind].Get()], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
      }
    }
    if (states[outputs[2].Get()] != D3D12_RESOURCE_STATE_COPY_SOURCE) {
      barriers.push_back(Transition(outputs[2].Get(), states[outputs[2].Get()], D3D12_RESOURCE_STATE_COPY_SOURCE));
    }
    for (const auto& pool : gpu.transient) {
      if (pool && states[pool.Get()] != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers.push_back(Transition(pool.Get(), states[pool.Get()], D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
      }
    }
    for (const Planned& plan : planned) {
      ID3D12Resource* mask = gpu.masks[plan.bake->mask_index].Get();
      if (states[mask] != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        barriers.push_back(Transition(mask, states[mask], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
      }
    }
    if (states[gpu.uvs.Get()] != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
      barriers.push_back(Transition(gpu.uvs.Get(), states[gpu.uvs.Get()], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    }
    if (!barriers.empty()) list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    list->CopyBufferRegion(readback.Get(), 0, outputs[2].Get(), 0, totals[3]);
    if (!why.empty()) {
      slow_report_ += std::format("{}[TW ray tracing] GPU micromap bake failed: {}.", slow_report_.empty() ? "" : "\n", why);
    }
    for (Planned& plan : planned) {
      if (plan.bake->gpu_failed) continue;
      MicromapBake& bake = *plan.bake;
      bake.gpu_arrays = outputs[0];
      bake.arrays_offset = plan.offsets[1];
      bake.gpu_descs = outputs[1];
      bake.descs_offset = plan.offsets[2];
      bake.gpu_indices = outputs[3];
      bake.indices_offset = plan.offsets[4];
      bake.gpu_readback = readback;
      bake.histogram_offset = plan.offsets[3];
      bake.histogram_bytes = static_cast<uint32_t>(plan.sizes[3]);
    }
#if defined(RENODX_RECORDER)
    // The recorder's journal (RENODX_TW_RECORD; the player's bakes again, LoadRecordingState): the post-dispatch infos (the
    // data's and descs' real sizes), data, descs and indices read back as well (AdvanceMicromaps keeps them as a CPU bake's);
    // the builds' ones readable again after.
    if (journal_on_ || player_) {
      static constexpr std::array<size_t, 4> JOURNALED = {5, 0, 1, 3};
      std::array<uint64_t, 4> bases = {};
      uint64_t journal_bytes = 0;
      for (size_t part = 0; part < JOURNALED.size(); ++part) {
        bases[part] = journal_bytes;
        journal_bytes += totals[JOURNALED[part] + 1];
      }
      if (ComPtr<ID3D12Resource> journal = Buffer(journal_bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST)) {
        barriers.clear();
        for (const size_t kind : JOURNALED) {
          const D3D12_RESOURCE_STATES state = (kind == 5) ? states[outputs[kind].Get()] : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
          if (state != D3D12_RESOURCE_STATE_COPY_SOURCE) barriers.push_back(Transition(outputs[kind].Get(), state, D3D12_RESOURCE_STATE_COPY_SOURCE));
        }
        list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
        for (size_t part = 0; part < JOURNALED.size(); ++part) {
          list->CopyBufferRegion(journal.Get(), bases[part], outputs[JOURNALED[part]].Get(), 0, totals[JOURNALED[part] + 1]);
        }
        barriers.clear();
        for (const size_t kind : {0u, 1u, 3u}) {
          barriers.push_back(Transition(outputs[kind].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
        }
        list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
        for (const Planned& plan : planned) {
          if (plan.bake->gpu_failed) continue;
          plan.bake->journal_readback = journal;
          plan.bake->journal_offsets = {bases[0] + plan.offsets[6], bases[1] + plan.offsets[1], bases[2] + plan.offsets[2], bases[3] + plan.offsets[4]};
          plan.bake->journal_limits = {plan.sizes[1], plan.sizes[2]};
        }
        releases_.push_back({.value = own_value_ + 1, .resource = std::move(journal)});
      }
    }
#endif
    // Every output (the histograms and the post-dispatch info too), the read-back and the constants kept until the GPU is
    // past this request.
    globals->Unmap(0, nullptr);
    for (auto& output : outputs) releases_.push_back({.value = own_value_ + 1, .resource = std::move(output)});
    releases_.push_back({.value = own_value_ + 1, .resource = std::move(readback)});
    releases_.push_back({.value = own_value_ + 1, .resource = std::move(globals)});
  }

  // A leaf mesh's micromaps from its UVs and mask (the bake thread): 4-state as the ray queries test them (bilinear,
  // wrapping, LEAF_ALPHA_CUTOFF), a micro-triangle about two texels across (at most leaf_micromap_level_, grass's
  // grass_micromap_level_), the same ones shared, wholly opaque or clear triangles the special indices (all of them so: no
  // micromaps, no array); none (no indices) when the SDK fails.
  void BakeMicromaps(MicromapBake* bake, ommBaker baker) const {
    ommCpuTextureMipDesc mip = ommCpuTextureMipDescDefault();
    mip.width = dlaa_bridge::RT_ATLAS_SIZE;
    mip.height = dlaa_bridge::RT_ATLAS_SIZE;
    mip.rowPitch = dlaa_bridge::RT_ATLAS_SIZE;
    mip.textureData = bake->mask_data.data();
    ommCpuTextureDesc texture_desc = ommCpuTextureDescDefault();
    texture_desc.format = ommCpuTextureFormat_UNORM8;
    texture_desc.mips = &mip;
    texture_desc.mipCount = 1;
    texture_desc.alphaCutoff = bake->cutoff;
    ommCpuTexture texture = nullptr;
    if (omm_.create_texture(baker, &texture_desc, &texture) != ommResult_SUCCESS) return;
    std::vector<uint32_t> corners(uint64_t{bake->triangles} * 3);
    std::iota(corners.begin(), corners.end(), 0u);
    ommCpuBakeInputDesc input = ommCpuBakeInputDescDefault();
    input.bakeFlags = ommCpuBakeFlags_EnableInternalThreads | ommCpuBakeFlags_Force32BitIndices;
    input.texture = texture;
    input.runtimeSamplerDesc = {.addressingMode = ommTextureAddressMode_Wrap, .filter = ommTextureFilterMode_Linear, .borderAlpha = 0.f};
    input.alphaMode = ommAlphaMode_Test;
    input.texCoordFormat = ommTexCoordFormat_UV16_FLOAT;
    input.texCoords = bake->uv_data.data();
    input.indexFormat = ommIndexFormat_UINT_32;
    input.indexBuffer = corners.data();
    input.indexCount = static_cast<uint32_t>(corners.size());
    input.alphaCutoff = bake->cutoff;
    input.maxSubdivisionLevel = bake->grass ? grass_micromap_level_ : leaf_micromap_level_;
    input.maxArrayDataSize = MICROMAP_MESH_BYTES;
    input.maxWorkloadSize = MICROMAP_WORKLOAD;
    ommCpuBakeResult result = nullptr;
    const ommCpuBakeResultDesc* desc = nullptr;
    if (omm_.bake(baker, &input, &result) == ommResult_SUCCESS && omm_.result_desc(result, &desc) == ommResult_SUCCESS
        && desc->indexFormat == ommIndexFormat_UINT_32 && desc->indexCount == bake->triangles) {
      static_assert(sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_DESC) == sizeof(ommCpuOpacityMicromapDesc));
      const auto* data = static_cast<const uint8_t*>(desc->arrayData);
      bake->data.assign(data, data + desc->arrayDataSize);
      bake->descs.resize(desc->descArrayCount);
      std::memcpy(bake->descs.data(), desc->descArray, bake->descs.size() * sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_DESC));
      for (uint32_t entry = 0; entry < desc->descArrayHistogramCount; ++entry) {
        const ommCpuOpacityMicromapUsageCount& usage = desc->descArrayHistogram[entry];
        bake->histogram.push_back({.Count = usage.count, .SubdivisionLevel = usage.subdivisionLevel,
                                   .Format = static_cast<D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT>(usage.format)});
      }
      const auto* indices = static_cast<const uint32_t*>(desc->indexBuffer);
      bake->indices.assign(indices, indices + desc->indexCount);
    }
    if (result != nullptr) omm_.destroy_result(result);
    omm_.destroy_texture(baker, texture);
  }
#endif

  // A capture's record flags (rt_scene.hlsli): alpha-tested leaves, thin cloth and sails (v11), smoke, grass, a leaves'
  // mesh (static or placed: LEAF_INSTANCE; a compact draw's flags with RT_DYNAMIC: its swaying copy).
  static uint32_t RecordFlags(const dlaa_bridge::RtCapture& capture, bool masked, uint32_t version) {
    return (masked ? RECORD_MASKED : 0u) | ((version >= 11 && (capture.flags & dlaa_bridge::RT_THIN) != 0) ? RECORD_THIN : 0u)
           | ((masked && (capture.flags & dlaa_bridge::RT_SMOKE) != 0) ? RECORD_SMOKE : 0u)
           | ((masked && (capture.flags & (dlaa_bridge::RT_GRASS | dlaa_bridge::RT_SOLID_CUT_OUT)) != 0) ? RECORD_GRASS : 0u)
           | ((masked && (capture.flags & (dlaa_bridge::RT_DYNAMIC | dlaa_bridge::RT_GRASS | dlaa_bridge::RT_SMOKE | dlaa_bridge::RT_SOLID_CUT_OUT)) == 0) ? RECORD_LEAF : 0u);
  }

  // A static mesh's instances' group: grass, leaves (alpha-tested), solid cut-outs (a fence, a thatched wall), or solid.
  static UINT InstanceMask(const Mesh& mesh) {
    if (mesh.grass) return GRASS_INSTANCE;
    if (mesh.solid_cut_out) return CUT_OUT_INSTANCE;
    return mesh.foliage ? LEAF_INSTANCE : SOLID_INSTANCE;
  }

  // A world-space BLAS into the slot's TLAS instances (leaves not opaque: their alpha is tested), its InstanceID its
  // first geometry's record; placed by `transform` (null: where it was captured); `place` its partitioned TLAS index
  // (kept in InstanceContributionToHitGroupIndex: no hit groups here).
  // Grass with micromaps (DXR 1.2) sets no 2-state flag here: it stays 4-state with its alpha tests (v108's 2-state grass
  // went solid, fields black in ATN's debug31); only Fast Leaf Shadows forces 2-state, as a ray flag (rt_scene.hlsli
  // SceneRayFlags).
  void AddInstance(Slot* slot, UINT* instance_count, D3D12_GPU_VIRTUAL_ADDRESS blas, uint32_t record, bool foliage,
                   const std::array<float, 12>* transform, uint32_t place, UINT mask) const {
    D3D12_RAYTRACING_INSTANCE_DESC& instance = slot->instance_data[(*instance_count)++];
    instance = {
        .InstanceID = record,
        .InstanceMask = mask,
        .InstanceContributionToHitGroupIndex = place,
        .Flags = static_cast<UINT>(D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE
                                   | (foliage ? 0u : static_cast<UINT>(D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE))),
        .AccelerationStructure = blas,
    };
    if (transform != nullptr) {
      std::memcpy(instance.Transform, transform->data(), sizeof(instance.Transform));
    } else {
      instance.Transform[0][0] = 1.f;
      instance.Transform[1][1] = 1.f;
      instance.Transform[2][2] = 1.f;
    }
  }

#if defined(RENODX_RECORDER)
  // The recorder's (SaveRecordingState and the rest above). A static mesh's chunk (then its transforms, its vertices and,
  // with MESH_MICROMAPS, its bake: MicromapBytes) and a cluster set's (then its clusters' descs and, with SET_MICROMAPS,
  // its bake).
  struct MeshRecord {
    uint64_t key = 0;
    uint64_t build = 0;
    uint32_t drawn_frame = 0;
    uint32_t triangles = 0;
    uint32_t record = 0;
    uint32_t normal_offset = 0;
    uint32_t uv_offset = 0;
    uint32_t first_instance = 0;
    uint32_t flags = 0;
    uint32_t transforms = 0;
  };
  static constexpr uint32_t MESH_TERRAIN = 1;
  static constexpr uint32_t MESH_FOLIAGE = 2;
  static constexpr uint32_t MESH_GRASS = 4;
  static constexpr uint32_t MESH_CUT_OUT = 8;
  static constexpr uint32_t MESH_PLACED = 16;
  static constexpr uint32_t MESH_MOVED = 32;
  static constexpr uint32_t MESH_MICROMAPS = 64;  // its BLAS built with its micromaps' indices (an array or special ones only)
  struct SetRecord {
    uint64_t key = 0;
    uint64_t generation = 0;
    uint32_t offset = 0;
    uint32_t units = 0;
    uint32_t vertices = 0;
    uint32_t clusters = 0;
    uint32_t triangles = 0;
    uint32_t template_first = UINT32_MAX;  // its templates' first place (UINT32_MAX: none)
    uint32_t flags = 0;
    uint32_t padding = 0;
  };
  static constexpr uint32_t SET_TRIED = 1;
  static constexpr uint32_t SET_MICROMAPS = 2;
  static constexpr uint32_t SET_TEMPLATE_MASKED = 4;
  // A bake's sizes before its data, descs, indices and histogram in a mesh's or set's chunk.
  struct MicromapBytes {
    uint64_t data = 0;
    uint32_t descs = 0;
    uint32_t indices = 0;
    uint32_t histogram = 0;
    uint32_t padding = 0;
  };
  // The journal's: a mesh's vertices as its build read them (`build` its mesh's then), or a micromap bake as its mesh's or
  // set's micromaps were built from it (`build` the mesh's build or the set's generation then).
  struct Journal {
    uint64_t build = 0;
    std::vector<std::array<float, 4>> vertices;
    bool unread = false;  // a GPU bake whose outputs were not read back (not recorded)
    std::vector<uint8_t> data;
    std::vector<D3D12_RAYTRACING_OPACITY_MICROMAP_DESC> descs;
    std::vector<uint32_t> indices;
    std::vector<D3D12_RAYTRACING_OPACITY_MICROMAP_HISTOGRAM_ENTRY> histogram;
  };
  // A recorded GPU resource: its name in the files, its member, its resting state, its recorded part (a buffer's: 0 bytes
  // all of it), how a player's scene makes it when it has none yet, the requests that write it (bits 1 << request: a
  // shared output each of their recorded requests writes into its output file, a history only at the last recorded frame).
  enum MadeBy : uint32_t { MADE_ALREADY, MADE_BY_HISTORY, MADE_BY_CACHE, MADE_BY_GROW };
  struct RecordedResource {
    const char* name = "";
    ComPtr<ID3D12Resource>* member = nullptr;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    uint64_t offset = 0;
    uint64_t bytes = 0;
    MadeBy made_by = MADE_ALREADY;
    History history = HISTORY_KINDS;
    uint32_t written_by = 0;
    bool output = false;
  };
  // A RecordedResources() row as ResourceRows() lists it at compile time (rt_serve.h classifies them): its member through
  // a function, and `index`, the RecordedFields() that index it (rt_serve.h's index rule).
  struct ResourceRow {
    const char* name = "";
    ComPtr<ID3D12Resource>* (*member)(RtScene12*) = nullptr;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    uint64_t offset = 0;
    uint64_t bytes = 0;
    MadeBy made_by = MADE_ALREADY;
    History history = HISTORY_KINDS;
    uint32_t written_by = 0;
    bool output = false;
    std::array<const char*, 3> index = {};
    // Temporal by its own tags: made as a history, cache or grown buffer, a history kind's, written by requests, an output.
    [[nodiscard]] constexpr bool Tagged() const { return made_by != MADE_ALREADY || history != HISTORY_KINDS || written_by != 0 || output; }
  };
  static constexpr uint32_t BY_TRACE = 2;
  static constexpr uint32_t BY_BOUNCE = 4;
  static constexpr uint32_t BY_REFLECTIONS = 8;
  // The resource-use log's names (UseTable, UseResource) past ResourceRows': each live table view's resource by its
  // descriptor (`sets` in a row, named <name>_<set>; no member: NRD's textures, named by the descriptor), then the buffers
  // the root descriptors, copies and builds use (NO_DESCRIPTOR).
  static constexpr UINT NO_DESCRIPTOR = DESCRIPTOR_COUNT;
  struct UseName {
    const char* name = "";
    UINT descriptor = NO_DESCRIPTOR;
    ComPtr<ID3D12Resource>* (*member)(RtScene12*) = nullptr;
    UINT sets = 1;
    bool nvapi = false;  // read by NVAPI's cluster and partitioned TLAS operations (not logged: external)
  };
  static std::string UseSetName(const UseName& entry, UINT set) {
    return (entry.sets > 1) ? std::format("{}_{}", entry.name, set) : std::string(entry.name);
  }
  // The first named resource `found` takes (ResourceRows, UseNames, the slots' buffers), its name; empty: none.
  template <typename Found>
  std::string UseFind(const Found& found) {
    for (const ResourceRow& row : ResourceRows()) {
      if (found(row.member(this)->Get())) return row.name;
    }
    for (const UseName& entry : UseNames()) {
      for (UINT set = 0; entry.member != nullptr && set < entry.sets; ++set) {
        if (found(entry.member(this)[set].Get())) return UseSetName(entry, set);
      }
    }
    for (size_t index = 0; index < slots_.size(); ++index) {
      for (const auto& [field, name] : {std::pair{&Slot::table, "table"}, std::pair{&Slot::left_out, "left_out"}, std::pair{&Slot::compact, "compact"},
                                        std::pair{&Slot::jobs, "jobs"}, std::pair{&Slot::clusters, "clusters"}, std::pair{&Slot::instances, "instances"},
                                        std::pair{&Slot::paths, "paths"}, std::pair{&Slot::lights, "lights"}, std::pair{&Slot::sizes, "sizes"},
                                        std::pair{&Slot::ptlas_device, "ptlas_device"}}) {
        if (found((slots_[index].*field).Get())) return std::format("slot{}_{}", index, name);
      }
    }
    return {};
  }
  // A pipeline's DXIL for the resource-use log (UseStem): every pipeline of the scene's root signature.
  void KeepCode(ID3D12PipelineState* pipeline, const void* code, size_t bytes) {
    const auto* first = static_cast<const uint8_t*>(code);
    use_code_.insert_or_assign(pipeline, std::vector<uint8_t>(first, first + bytes));
  }
  // A resource's bytes on their way to the CPU (CopyToReadback): a buffer's part, or a texture's rows from the top (each
  // subresource's footprint in the read-back).
  struct Readback {
    std::string name;
    ComPtr<ID3D12Resource> buffer;
    D3D12_RESOURCE_DESC desc = {};
    uint64_t offset = 0;
    uint64_t bytes = 0;
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints;
    std::vector<UINT> rows;
    std::vector<UINT64> row_bytes;
  };
  // RecordFrames' recording: its frames, folder, the request numbers' frame, what this thread noted; its writer's (read
  // once it is waited for): the bytes written, what failed, the inputs' hashes as last written (an input unchanged since
  // is left out).
  struct Recording {
    bool on = false;
    bool inputs = false;
    bool histories = false;
    uint32_t first = 0;
    uint32_t last = 0;
    std::filesystem::path folder;
    uint32_t frame = UINT32_MAX;
    uint32_t index = 0;
    uint32_t requests = 0;
    std::string notes;
    uint64_t bytes = 0;
    std::string failed;
    std::unordered_map<std::string, uint64_t> hashes;
    uint32_t name_shift = 0;  // the played frames' numbers past the recording's (a resident's dump)
  };
  // The last trace's scene for its digest (Record): its CPU's descs, all its TLAS's, whether rt_place wrote them, the
  // smoke's desc, its slot.
  struct RecordedScene {
    uint32_t cpu_instances = 0;
    uint32_t tlas_instances = 0;
    bool placed = false;
    bool smoke = false;
    size_t slot = 0;
  };

  // The recorded GPU resources (ResourceRows), their members this scene's.
  std::vector<RecordedResource> RecordedResources() {
    std::vector<RecordedResource> resources;
    for (const ResourceRow& row : ResourceRows()) {
      resources.push_back({.name = row.name, .member = row.member(this), .state = row.state, .offset = row.offset, .bytes = row.bytes,
                           .made_by = row.made_by, .history = row.history, .written_by = row.written_by, .output = row.output});
    }
    return resources;
  }

  // The snapshot's layout (FileHeader::layout): its fields' names and sizes, the resources' names, the chunks' structs.
  uint64_t RecordingLayout() {
    uint64_t hash = 0xCBF29CE484222325ull;
    const auto mix = [&](std::string_view text, uint64_t size) {
      for (const char character : text) hash = (hash ^ static_cast<uint8_t>(character)) * 0x100000001B3ull;
      hash = (hash ^ size) * 0x100000001B3ull;
    };
    std::apply([&](const auto&... field) { (mix(field.first, sizeof(this->*field.second)), ...); }, RecordedFields());
    for (const RecordedResource& resource : RecordedResources()) mix(resource.name, resource.bytes);
    mix("chunks", sizeof(MeshRecord) + (sizeof(SetRecord) << 8) + (sizeof(MicromapBytes) << 16) + (sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_DESC) << 24));
    return hash;
  }

  // A recording file's header: this helper's bridge, layout and build (its exe's size and time).
  recording::FileHeader RecordingHeader(uint32_t frame, uint32_t request) {
    recording::FileHeader header = {.bridge_version = dlaa_bridge::VERSION, .shared_bytes = static_cast<uint32_t>(sizeof(dlaa_bridge::Shared)),
                                    .layout = RecordingLayout(), .frame = frame, .request = request};
    std::wstring exe(MAX_PATH, L'\0');
    exe.resize(GetModuleFileNameW(nullptr, exe.data(), MAX_PATH));
    std::error_code ignored;
    std::format_to_n(header.helper, sizeof(header.helper) - 1, "{} bytes, {}", std::filesystem::file_size(exe, ignored),
                     std::filesystem::last_write_time(exe, ignored).time_since_epoch().count());
    return header;
  }

  // A 64-bit hash of `bytes` (eight at a time): the inputs' (WriteRecordingFile) and the digest's.
  static uint64_t HashBytes(std::span<const std::byte> bytes) {
    uint64_t hash = 0xCBF29CE484222325ull ^ bytes.size();
    size_t at = 0;
    for (; at + 8 <= bytes.size(); at += 8) {
      uint64_t word = 0;
      std::memcpy(&word, bytes.data() + at, 8);
      hash = (hash ^ word) * 0x100000001B3ull;
      hash ^= hash >> 29;
    }
    for (; at < bytes.size(); ++at) hash = (hash ^ static_cast<uint8_t>(bytes[at])) * 0x100000001B3ull;
    return hash;
  }

  // A chunk (its header, name, bytes) at the end of `chunks`.
  static void AddChunk(std::vector<uint8_t>* chunks, uint32_t kind, std::string_view name, std::span<const std::byte> bytes) {
    const recording::ChunkHeader header = {.kind = kind, .name_bytes = static_cast<uint32_t>(name.size()), .bytes = bytes.size()};
    const auto* header_bytes = reinterpret_cast<const uint8_t*>(&header);
    chunks->insert(chunks->end(), header_bytes, header_bytes + sizeof(header));
    chunks->insert(chunks->end(), name.begin(), name.end());
    const auto* data = reinterpret_cast<const uint8_t*>(bytes.data());
    chunks->insert(chunks->end(), data, data + bytes.size());
  }

  // `parts` one after the other, then a bake's (MicromapBytes and its arrays) when there is one.
  static std::vector<std::byte> JoinBytes(std::initializer_list<std::span<const std::byte>> parts, const Journal* micromaps) {
    std::vector<std::byte> joined;
    const auto append = [&](std::span<const std::byte> part) { joined.insert(joined.end(), part.begin(), part.end()); };
    for (const auto part : parts) append(part);
    if (micromaps == nullptr) return joined;
    const MicromapBytes sizes = {.data = micromaps->data.size(), .descs = static_cast<uint32_t>(micromaps->descs.size()),
                                 .indices = static_cast<uint32_t>(micromaps->indices.size()), .histogram = static_cast<uint32_t>(micromaps->histogram.size())};
    append(std::as_bytes(std::span(&sizes, 1)));
    append(std::as_bytes(std::span(micromaps->data)));
    append(std::as_bytes(std::span(micromaps->descs)));
    append(std::as_bytes(std::span(micromaps->indices)));
    append(std::as_bytes(std::span(micromaps->histogram)));
    return joined;
  }

  // A bake's micromaps built again on `list` (LoadRecordingState): its inputs uploaded as Record uploads a CPU bake's (the
  // data, descs, indices at 256-byte places), its array (none when every triangle has a special index); the scratch kept in
  // `keep` until the list is done. False: a truncated bake or no memory.
  bool BuildRecordedMicromaps(ID3D12GraphicsCommandList4* list, std::span<const uint8_t> bytes, ComPtr<ID3D12Resource>* array, ComPtr<ID3D12Resource>* inputs,
                              uint64_t* indices_offset, std::vector<ComPtr<ID3D12Resource>>* keep) {
    MicromapBytes sizes = {};
    if (bytes.size() < sizeof(sizes)) return false;
    std::memcpy(&sizes, bytes.data(), sizeof(sizes));
    const uint64_t descs_bytes = uint64_t{sizes.descs} * sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_DESC);
    const uint64_t histogram_bytes = uint64_t{sizes.histogram} * sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_HISTOGRAM_ENTRY);
    if (bytes.size() < sizeof(sizes) + sizes.data + descs_bytes + (uint64_t{sizes.indices} * 4) + histogram_bytes) return false;
    const uint8_t* data = bytes.data() + sizeof(sizes);
    const uint64_t descs_offset = Align(sizes.data, 256);
    *indices_offset = Align(descs_offset + descs_bytes, 256);
    *inputs = Buffer(*indices_offset + (uint64_t{sizes.indices} * 4), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    uint8_t* mapped = nullptr;
    if (!*inputs || FAILED((*inputs)->Map(0, nullptr, reinterpret_cast<void**>(&mapped)))) return false;
    std::memcpy(mapped, data, sizes.data);
    std::memcpy(mapped + descs_offset, data + sizes.data, descs_bytes);
    std::memcpy(mapped + *indices_offset, data + sizes.data + descs_bytes, uint64_t{sizes.indices} * 4);
    (*inputs)->Unmap(0, nullptr);
    if (sizes.histogram == 0) return true;
    const auto* histogram =
        reinterpret_cast<const D3D12_RAYTRACING_OPACITY_MICROMAP_HISTOGRAM_ENTRY*>(data + sizes.data + descs_bytes + (uint64_t{sizes.indices} * 4));
    const D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_DESC array_desc = {
        .NumOmmHistogramEntries = sizes.histogram,
        .pOmmHistogram = histogram,
        .InputBuffer = (*inputs)->GetGPUVirtualAddress(),
        .PerOmmDescs = {.StartAddress = (*inputs)->GetGPUVirtualAddress() + descs_offset, .StrideInBytes = sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_DESC)},
    };
    const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS array_inputs = {
        .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_OPACITY_MICROMAP_ARRAY,
        .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
        .NumDescs = 1,
        .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
        .pOpacityMicromapArrayDesc = &array_desc,
    };
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
    device_->GetRaytracingAccelerationStructurePrebuildInfo(&array_inputs, &info);
    *array = Buffer(info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE,
                    D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    ComPtr<ID3D12Resource> scratch = Buffer(std::max<uint64_t>(info.ScratchDataSizeInBytes, 256), D3D12_HEAP_TYPE_DEFAULT,
                                            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (!*array || !scratch) return false;
    const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build = {.DestAccelerationStructureData = (*array)->GetGPUVirtualAddress(),
                                                                       .Inputs = array_inputs,
                                                                       .ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress()};
    list->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
    keep->push_back(std::move(scratch));
    return true;
  }

  // A recorded resource's bytes as StageRecorded laid them out for their target: an upload buffer (the player's: a copy of
  // it in video memory), a buffer's part (its offset and length there) or a texture's subresources' footprints.
  struct Staged {
    ComPtr<ID3D12Resource> buffer;
    uint64_t offset = 0;
    uint64_t length = 0;
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints;
  };

  // A recording's file (ReadPlaybackFile; its bytes kept in `data`): its header, its block, its sets' data and its resources
  // by name.
  struct PlaybackFile {
    std::vector<uint8_t> data;
    recording::FileHeader header;
    std::span<const uint8_t> block;
    std::span<const uint8_t> sets;
    std::vector<std::pair<std::string, std::span<const uint8_t>>> resources;
  };
  // `path` read into `file`, its header checked (this format and a bridge version this helper plays). False: `report` says why.
  static bool ReadPlaybackFile(const std::filesystem::path& path, PlaybackFile* file, std::string* report) {
    std::error_code error;
    const uint64_t size = std::filesystem::file_size(path, error);
    file->data.resize(error ? 0u : size);
    if (std::ifstream in(path, std::ios::binary); error || file->data.size() < sizeof(file->header)
                                                   || !in.read(reinterpret_cast<char*>(file->data.data()), static_cast<std::streamsize>(file->data.size()))) {
      *report = std::format("{} could not be read", path.string());
      return false;
    }
    std::memcpy(&file->header, file->data.data(), sizeof(file->header));
    if (file->header.magic != recording::MAGIC || file->header.format != recording::FORMAT || file->header.bridge_version < dlaa_bridge::OLDEST_VERSION
        || file->header.bridge_version > dlaa_bridge::VERSION) {
      *report = std::format("{} is another build's (format {}, bridge v{}; this helper: {}, v{})", path.string(), file->header.format,
                            file->header.bridge_version, recording::FORMAT, dlaa_bridge::VERSION);
      return false;
    }
    for (uint64_t at = sizeof(file->header);;) {
      recording::ChunkHeader chunk = {};
      if (at + sizeof(chunk) > file->data.size()) break;
      std::memcpy(&chunk, file->data.data() + at, sizeof(chunk));
      at += sizeof(chunk);
      if (chunk.kind == recording::CHUNK_END) return true;
      if (at + chunk.name_bytes + chunk.bytes > file->data.size()) break;
      std::string name(reinterpret_cast<const char*>(file->data.data() + at), chunk.name_bytes);
      const std::span<const uint8_t> bytes(file->data.data() + at + chunk.name_bytes, chunk.bytes);
      at += chunk.name_bytes + chunk.bytes;
      if (chunk.kind == recording::CHUNK_BLOCK) file->block = bytes;
      if (chunk.kind == recording::CHUNK_SETS) file->sets = bytes;
      if (chunk.kind == recording::CHUNK_TEXTURE || chunk.kind == recording::CHUNK_BUFFER) file->resources.emplace_back(std::move(name), bytes);
    }
    *report = std::format("{} is truncated", path.string());
    return false;
  }
  // A loaded request (LoadPlayback): its file, header, block's and cluster sets' bytes (the block's `block_bytes` first), its
  // inputs staged in video memory for their shared textures.
  struct PlaybackRequest {
    std::filesystem::path path;
    recording::FileHeader header;
    std::vector<uint8_t> data;
    size_t block_bytes = 0;
    std::vector<std::pair<ID3D12Resource*, Staged>> inputs;
  };
  // The block's handles by the shared textures' names (the recorded inputs' and outputs').
  static constexpr std::array<std::pair<const char*, uint64_t dlaa_bridge::Shared::*>, 19> PLAYER_HANDLES = {{
      {"vertices", &dlaa_bridge::Shared::rt_vertices},     {"positions", &dlaa_bridge::Shared::rt_positions},
      {"sun", &dlaa_bridge::Shared::rt_sun},               {"visibility", &dlaa_bridge::Shared::rt_visibility},
      {"ambient", &dlaa_bridge::Shared::rt_ambient},       {"atlas", &dlaa_bridge::Shared::rt_atlas},
      {"light", &dlaa_bridge::Shared::rt_light},           {"bounce", &dlaa_bridge::Shared::rt_bounce},
      {"light_mask", &dlaa_bridge::Shared::rt_light_mask}, {"fog", &dlaa_bridge::Shared::rt_fog},
      {"fog_lights", &dlaa_bridge::Shared::rt_fog_lights}, {"tint", &dlaa_bridge::Shared::rt_tint},
      {"color", &dlaa_bridge::Shared::color},              {"normals", &dlaa_bridge::Shared::normals},
      {"albedo", &dlaa_bridge::Shared::diffuse_albedo},    {"roughness", &dlaa_bridge::Shared::roughness},
      {"reflection", &dlaa_bridge::Shared::rt_reflection}, {"specular", &dlaa_bridge::Shared::rt_specular},
      {"hit_distance", &dlaa_bridge::Shared::rr_hit_distance},
  }};
  // The loaded playback: the player's shared textures by name and their handles (PLAYER_HANDLES' order), the requests in
  // frame order, the bridge version, the first and last recorded frames.
  struct Playback {
    std::unordered_map<std::string, ComPtr<ID3D12Resource>> textures;
    std::array<uint64_t, PLAYER_HANDLES.size()> handles = {};
    std::vector<PlaybackRequest> requests;
    uint32_t version = 0;
    uint32_t first_frame = 0;
    uint32_t last_frame = 0;
  };
  // A request's block (~1 MB: off the stack) as recorded, the player's handles in it, its frame `frame_offset` later.
  [[nodiscard]] std::unique_ptr<dlaa_bridge::Shared> PlaybackBlock(const PlaybackRequest& request, uint32_t frame_offset) const {
    auto block = std::make_unique<dlaa_bridge::Shared>();
    std::memcpy(block.get(), request.data.data(), std::min(request.block_bytes, sizeof(dlaa_bridge::Shared)));
    for (size_t index = 0; index < PLAYER_HANDLES.size(); ++index) block.get()->*PLAYER_HANDLES[index].second = playback_.handles[index];
    block->rt_frame += frame_offset;
    return block;
  }

  // A read-back's bytes as a recording's chunk holds them (a buffer's BufferHeader and part, a texture's TextureHeader and
  // rows), its buffer let go. False: it could not be mapped.
  static bool ReadbackPayload(Readback* readback, std::vector<uint8_t>* payload) {
    uint8_t* mapped = nullptr;
    if (FAILED(readback->buffer->Map(0, nullptr, reinterpret_cast<void**>(&mapped)))) return false;
    payload->clear();
    if (readback->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
      const recording::BufferHeader part = {.bytes = readback->desc.Width, .offset = readback->offset};
      payload->resize(sizeof(part) + readback->bytes);
      std::memcpy(payload->data(), &part, sizeof(part));
      std::memcpy(payload->data() + sizeof(part), mapped, readback->bytes);
    } else {
      const recording::TextureHeader texture = {.format = static_cast<uint32_t>(readback->desc.Format), .width = static_cast<uint32_t>(readback->desc.Width),
                                                .height = readback->desc.Height, .slices = static_cast<uint32_t>(readback->footprints.size()),
                                                .row_bytes = static_cast<uint32_t>(readback->row_bytes[0]), .rows = readback->rows[0],
                                                .mips = readback->desc.MipLevels};
      payload->resize(sizeof(texture));
      std::memcpy(payload->data(), &texture, sizeof(texture));
      for (size_t subresource = 0; subresource < readback->footprints.size(); ++subresource) {
        for (UINT row = 0; row < readback->rows[subresource]; ++row) {
          const uint8_t* from = mapped + readback->footprints[subresource].Offset + (uint64_t{row} * readback->footprints[subresource].Footprint.RowPitch);
          payload->insert(payload->end(), from, from + readback->row_bytes[subresource]);
        }
      }
    }
    const D3D12_RANGE none = {};
    readback->buffer->Unmap(0, &none);
    readback->buffer.Reset();
    return true;
  }

  // The resident's timestamps (ServeTiming, ReadServeTimes): a list's start (a request's, its inputs' copy's), a Mark()
  // pass's start (after a UAV barrier: the passes before it done), a list's end (its stamps resolved there).
  enum ServeStampKind : uint8_t { SERVE_START, SERVE_INPUTS, SERVE_MARK, SERVE_END };
  struct ServeMarkEntry {
    std::string row;
    ServeStampKind kind = SERVE_MARK;
  };
  static constexpr uint32_t SERVE_STAMPS = 512;
  // A timestamp on `list` (a mark's only on the list whose start was stamped last; none past SERVE_STAMPS).
  void ServeStamp(ID3D12GraphicsCommandList* list, ServeStampKind kind, const wchar_t* pass = nullptr) {
    if (serve_timing_ == SERVE_TIMING_OFF || !serve_stamps_) return;
    if (kind == SERVE_START || kind == SERVE_INPUTS) {
      serve_list_ = list;
      serve_list_first_ = static_cast<uint32_t>(serve_marks_.size());
    }
    if (list != serve_list_ || serve_marks_.size() >= SERVE_STAMPS) return;
    if (kind == SERVE_MARK) {
      const D3D12_RESOURCE_BARRIER uav_barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV};
      list->ResourceBarrier(1, &uav_barrier);
    }
    const auto index = static_cast<uint32_t>(serve_marks_.size());
    list->EndQuery(serve_stamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index);
    ServeMarkEntry& entry = serve_marks_.emplace_back(ServeMarkEntry{.kind = kind});
    for (; pass != nullptr && *pass != L'\0'; ++pass) entry.row += static_cast<char>(*pass);
    if (kind == SERVE_END) {
      list->ResolveQueryData(serve_stamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, serve_list_first_, index + 1 - serve_list_first_, serve_readback_.Get(),
                             uint64_t{serve_list_first_} * sizeof(uint64_t));
      serve_list_ = nullptr;
    }
  }
  // Mark()'s pass for the resident: its timestamp (SERVE_TIMING_PASSES), then the burn when it is the burn's pass.
  void ServeMark(ID3D12GraphicsCommandList* list, const wchar_t* pass) {
    if (uses_) {
      uses_->Span(list, pass);
    }
    if (serve_timing_ == SERVE_TIMING_PASSES) ServeStamp(list, SERVE_MARK, pass);
    if (burn_loops_ == 0 || burn_pass_ != pass) return;
    ++burn_runs_;
    const D3D12_RESOURCE_BARRIER uav_barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV};
    list->SetPipelineState(burn_pipeline_.Get());
    for (uint32_t loop = 0; loop < burn_loops_; ++loop) {
      list->Dispatch(1, 1, 1);
      list->ResourceBarrier(1, &uav_barrier);
    }
  }
  // The moments on their way (StartMoments): the outputs, their root signature, descriptors and pipeline, frames added.
  struct Moments {
    std::vector<MomentOutput> outputs;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12DescriptorHeap> heap;
    ComPtr<ID3D12PipelineState> pipeline;
    uint32_t frames = 0;
  };

  // A recorded resource's bytes (a CHUNK_TEXTURE's or CHUNK_BUFFER's) copied on `list` into `target`, resting in `state`
  // (StageRecorded, CopyStaged); the upload kept in `uploads` until the list is done. False: `why` says what did not fit.
  bool UploadRecorded(ID3D12GraphicsCommandList* list, std::string_view name, ID3D12Resource* target, D3D12_RESOURCE_STATES state,
                      std::span<const uint8_t> bytes, std::vector<ComPtr<ID3D12Resource>>* uploads, std::string* why) {
    Staged staged;
    if (!StageRecorded(name, target, bytes, &staged, why)) return false;
    CopyStaged(list, staged, target, state);
    uploads->push_back(std::move(staged.buffer));
    return true;
  }

  // A recorded resource's bytes in a new upload buffer laid out for `target`: a buffer's part at its offset (the recorded
  // buffer's size this one's), a texture's subresources (or the top rows of its one: the vertex texture's) by their
  // footprints here, its format, size and subresources the recorded ones. False: `why` says what did not fit.
  bool StageRecorded(std::string_view name, ID3D12Resource* target, std::span<const uint8_t> bytes, Staged* staged, std::string* why) {
    D3D12_RESOURCE_DESC desc = target->GetDesc();
    uint8_t* mapped = nullptr;
    const auto staging = [&](uint64_t upload_bytes) {
      staged->buffer = Buffer(std::max<uint64_t>(upload_bytes, 256), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
      return staged->buffer && SUCCEEDED(staged->buffer->Map(0, nullptr, reinterpret_cast<void**>(&mapped)));
    };
    if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
      recording::BufferHeader part = {};
      if (bytes.size() >= sizeof(part)) std::memcpy(&part, bytes.data(), sizeof(part));
      const uint64_t length = (bytes.size() >= sizeof(part)) ? bytes.size() - sizeof(part) : 0u;
      if (bytes.size() < sizeof(part) || part.bytes != desc.Width || part.offset + length > desc.Width || !staging(length)) {
        *why = std::format("{}: {} bytes at {} of a {}-byte buffer recorded, {} bytes here", name, length, part.offset, part.bytes, desc.Width);
        return false;
      }
      std::memcpy(mapped, bytes.data() + sizeof(part), length);
      staged->offset = part.offset;
      staged->length = length;
    } else {
      recording::TextureHeader texture = {};
      if (bytes.size() >= sizeof(texture)) std::memcpy(&texture, bytes.data(), sizeof(texture));
      const UINT height = desc.Height;
      const UINT subresources = desc.DepthOrArraySize * desc.MipLevels;
      if (subresources == 1 && texture.rows != 0 && texture.rows < desc.Height) desc.Height = texture.rows;
      std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(subresources);
      std::vector<UINT> rows(subresources);
      std::vector<UINT64> row_bytes(subresources);
      uint64_t total = 0;
      device_->GetCopyableFootprints(&desc, 0, subresources, 0, footprints.data(), rows.data(), row_bytes.data(), &total);
      uint64_t packed = 0;
      for (UINT subresource = 0; subresource < subresources; ++subresource) packed += uint64_t{rows[subresource]} * row_bytes[subresource];
      if (texture.format != desc.Format || texture.width != desc.Width || texture.height != height || texture.slices != subresources
          || texture.rows != rows[0] || texture.row_bytes != row_bytes[0] || bytes.size() < sizeof(texture) + packed || !staging(total)) {
        *why = std::format("{}: recorded {} x {} ({} subresources, format {}), here {} x {} ({}, format {})", name, texture.width, texture.height,
                           texture.slices, texture.format, desc.Width, height, subresources, static_cast<uint32_t>(desc.Format));
        return false;
      }
      const uint8_t* from = bytes.data() + sizeof(texture);
      for (UINT subresource = 0; subresource < subresources; ++subresource) {
        for (UINT row = 0; row < rows[subresource]; ++row) {
          std::memcpy(mapped + footprints[subresource].Offset + (uint64_t{row} * footprints[subresource].Footprint.RowPitch), from, row_bytes[subresource]);
          from += row_bytes[subresource];
        }
      }
      staged->footprints = std::move(footprints);
    }
    staged->buffer->Unmap(0, nullptr);
    return true;
  }

  // `staged` (StageRecorded's for `target`) copied on `list` into `target`, resting in `state`.
  void CopyStaged(ID3D12GraphicsCommandList* list, const Staged& staged, ID3D12Resource* target, D3D12_RESOURCE_STATES state) {
    const auto to_copy = Transition(target, state, D3D12_RESOURCE_STATE_COPY_DEST);
    list->ResourceBarrier(1, &to_copy);
    if (staged.footprints.empty()) list->CopyBufferRegion(target, staged.offset, staged.buffer.Get(), 0, staged.length);
    for (UINT subresource = 0; subresource < staged.footprints.size(); ++subresource) {
      const D3D12_TEXTURE_COPY_LOCATION destination = {.pResource = target, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, .SubresourceIndex = subresource};
      const D3D12_TEXTURE_COPY_LOCATION source = {.pResource = staged.buffer.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
                                                  .PlacedFootprint = staged.footprints[subresource]};
      list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    }
    const auto copied = Transition(target, D3D12_RESOURCE_STATE_COPY_DEST, state);
    list->ResourceBarrier(1, &copied);
  }

  // A slot's journal copies (its last trace's new static meshes' vertices, done) into the journal; the bakes of meshes and
  // sets gone or built again since let go (a few thousand kept at most).
  void TakeJournalReads(Slot* slot) {
    if (!slot->journal_reads.empty()) {
      uint8_t* mapped = nullptr;
      if (SUCCEEDED(slot->journal_readback->Map(0, nullptr, reinterpret_cast<void**>(&mapped)))) {
        for (const Slot::JournalRead& read : slot->journal_reads) {
          Journal& kept = journal_[read.key];
          kept = {.build = read.build};
          kept.vertices.resize(read.vertices);
          std::memcpy(kept.vertices.data(), mapped + read.offset, uint64_t{read.vertices} * 16);
        }
        const D3D12_RANGE written = {};
        slot->journal_readback->Unmap(0, &written);
      }
      slot->journal_reads.clear();
      slot->journal_readback.Reset();
    }
    if (journal_.size() > meshes_.size() + 4096) std::erase_if(journal_, [&](const auto& entry) { return !meshes_.contains(entry.first); });
    if (micromap_journal_.size() > meshes_.size() + 4096) {
      std::erase_if(micromap_journal_, [&](const auto& entry) {
        const auto mesh = meshes_.find(entry.first);
        return mesh == meshes_.end() || mesh->second.build != entry.second.build;
      });
    }
    if (set_journal_.size() > sets_.size() + 4096) {
      std::erase_if(set_journal_, [&](const auto& entry) {
        const auto set = sets_.find(entry.first);
        return set == sets_.end() || set->second.generation != entry.second.build;
      });
    }
  }

  // The recorder's command list, reset (its last list done); null: none.
  ID3D12GraphicsCommandList4* RecorderList() {
    if (!recorder_allocator_
        && (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(recorder_allocator_.GetAddressOf())))
            || FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, recorder_allocator_.Get(), nullptr,
                                                 IID_PPV_ARGS(recorder_list_.GetAddressOf())))
            || FAILED(recorder_list_->Close()))) {
      recorder_allocator_.Reset();
      return nullptr;
    }
    if (!WaitFor(recorder_value_) || FAILED(recorder_allocator_->Reset()) || FAILED(recorder_list_->Reset(recorder_allocator_.Get(), nullptr))) return nullptr;
    return recorder_list_.Get();
  }

  // The recorder's list run on the queue (after what is on it); its fence value.
  uint64_t SubmitRecorderList(ID3D12GraphicsCommandList4* list) {
    if (SUCCEEDED(list->Close())) {
      ID3D12CommandList* lists[] = {list};
      queue_->ExecuteCommandLists(1, lists);
    }
    queue_->Signal(own_fence_.Get(), ++own_value_);
    recorder_value_ = own_value_;
    return own_value_;
  }

  // `resource` (resting in `state`) copied on `list` into a new read-back: a buffer's `bytes` from `offset` (0: the rest), a
  // texture's top `rows` (0: all) of each subresource. No memory: left out (the recording's notes say it).
  void CopyToReadback(ID3D12GraphicsCommandList* list, const char* name, ID3D12Resource* resource, D3D12_RESOURCE_STATES state,
                      std::vector<Readback>* readbacks, uint64_t offset = 0, uint64_t bytes = 0, uint32_t rows = 0) {
    Readback readback = {.name = name, .desc = resource->GetDesc(), .offset = offset};
    const bool buffer = readback.desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
    uint64_t total = 0;
    if (buffer) {
      readback.bytes = (bytes != 0) ? std::min(bytes, readback.desc.Width - offset) : readback.desc.Width - offset;
      total = readback.bytes;
    } else {
      D3D12_RESOURCE_DESC desc = readback.desc;
      if (rows != 0) desc.Height = std::min<UINT>(rows, desc.Height);
      const UINT subresources = desc.DepthOrArraySize * desc.MipLevels;
      readback.footprints.resize(subresources);
      readback.rows.resize(subresources);
      readback.row_bytes.resize(subresources);
      device_->GetCopyableFootprints(&desc, 0, subresources, 0, readback.footprints.data(), readback.rows.data(), readback.row_bytes.data(), &total);
    }
    readback.buffer = Buffer(std::max<uint64_t>(total, 256), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    if (!readback.buffer) {
      recording_.notes += std::format("{} (no memory for {} MB); ", name, total >> 20);
      return;
    }
    const auto to_copy = Transition(resource, state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->ResourceBarrier(1, &to_copy);
    if (buffer) {
      list->CopyBufferRegion(readback.buffer.Get(), 0, resource, offset, readback.bytes);
    } else {
      for (UINT subresource = 0; subresource < readback.footprints.size(); ++subresource) {
        const D3D12_TEXTURE_COPY_LOCATION destination = {.pResource = readback.buffer.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
                                                         .PlacedFootprint = readback.footprints[subresource]};
        const D3D12_TEXTURE_COPY_LOCATION source = {.pResource = resource, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, .SubresourceIndex = subresource};
        const D3D12_BOX box = {.left = 0, .top = 0, .front = 0, .right = readback.footprints[subresource].Footprint.Width,
                               .bottom = readback.rows[subresource], .back = 1};
        list->CopyTextureRegion(&destination, 0, 0, 0, &source, &box);
      }
    }
    const auto copied = Transition(resource, D3D12_RESOURCE_STATE_COPY_SOURCE, state);
    list->ResourceBarrier(1, &copied);
    readbacks->push_back(std::move(readback));
  }

  // `job` on the recording's writer once the queue is past `value` (in order after the jobs before; a removed or hung
  // device's recording is incomplete).
  void QueueRecordingJob(uint64_t value, std::function<void()> job) {
    recording_writer_ = std::async(std::launch::async, [this, value, job = std::move(job), previous = std::move(recording_writer_)]() mutable {
      if (previous.valid()) previous.wait();
      const HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
      const bool done = WaitForGpu(own_fence_.Get(), event, value, 60000) && own_fence_->GetCompletedValue() != UINT64_MAX;
      if (event != nullptr) CloseHandle(event);
      if (!done) {
        recording_.failed += "the device was removed or hung during the recording; ";
        return;
      }
      job();
    });
  }

  // A recording's file: its header, the CPU's chunks, each read-back's chunk (with `skip_unchanged` one the same as the
  // last written under its name left out: the inputs a request did not change), the end. Its read-backs let go.
  void WriteRecordingFile(const std::filesystem::path& path, const recording::FileHeader& header, const std::vector<uint8_t>& chunks,
                          std::vector<Readback>* readbacks, bool skip_unchanged) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(chunks.data()), static_cast<std::streamsize>(chunks.size()));
    uint64_t written = sizeof(header) + chunks.size();
    for (Readback& readback : *readbacks) {
      std::vector<uint8_t> payload;
      if (!ReadbackPayload(&readback, &payload)) {
        recording_.failed += readback.name + " could not be read; ";
        continue;
      }
      const bool buffer = readback.desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
      if (skip_unchanged) {
        const uint64_t hash = HashBytes(std::as_bytes(std::span(payload)));
        if (const auto last = recording_.hashes.find(readback.name); last != recording_.hashes.end() && last->second == hash) continue;
        recording_.hashes[readback.name] = hash;
      }
      const recording::ChunkHeader chunk = {.kind = buffer ? recording::CHUNK_BUFFER : recording::CHUNK_TEXTURE,
                                            .name_bytes = static_cast<uint32_t>(readback.name.size()), .bytes = payload.size()};
      file.write(reinterpret_cast<const char*>(&chunk), sizeof(chunk));
      file.write(readback.name.data(), static_cast<std::streamsize>(readback.name.size()));
      file.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
      written += sizeof(chunk) + readback.name.size() + payload.size();
    }
    const recording::ChunkHeader end = {};
    file.write(reinterpret_cast<const char*>(&end), sizeof(end));
    if (!file) recording_.failed += path.filename().string() + " could not be written; ";
    recording_.bytes += written + sizeof(end);
  }

  // The lock step: while a recording's requests run and in a play-back, what the GPU's progress, the bake threads and the
  // rooms' growth give a request is the requests' alone (Trace waits for every earlier request and for them).
  [[nodiscard]] bool Lockstep() const { return recording_.on || player_; }
  // A snapshot waiting for the bakes in flight (its last try a few requests ago at most): no new bakes meanwhile.
  [[nodiscard]] bool SnapshotWaiting() const { return snapshot_waited_ != 0 && own_value_ < snapshot_waited_ + 8; }
#if defined(RENODX_NRD)
  std::array<NrdSignal*, 4> NrdSignals() { return {&nrd_sun_, &nrd_ambient_, &nrd_bounce_, &nrd_reflections_}; }
#endif

  // A recorded frame's request (RecordFrames; Trace, Paths): its block and new sets' data, the inputs it read (the vertex
  // texture's rows it copied), its kind's shared outputs and at the last recorded frame its kind's histories, copied on the
  // queue after its list and before its fence value (the addon's next writes wait for that); a trace's scene digest: its
  // TLAS's descs with each BLAS named (a mesh by its key) order-free, the static records, the meshes and sets.
  void RecordRequest(const dlaa_bridge::Shared& shared, uint32_t version, std::span<const uint8_t> cluster_data, uint32_t request) {
    const uint32_t frame = shared.rt_frame;
    if (!recording_.on || frame < recording_.first || frame > recording_.last) return;
    if (frame != recording_.frame) {
      recording_.frame = frame;
      recording_.index = 0;
    }
    // Named by the recording's frame (a resident's dump: its played frames are past the recording's, Recording::name_shift).
    const uint32_t named = frame - recording_.name_shift;
    const std::string stem = std::format("{}_{}_{}", named, recording_.index++, (request == 1) ? "trace" : ((request == 2) ? "bounce" : "reflections"));
    ++recording_.requests;
    ID3D12GraphicsCommandList4* list = RecorderList();
    if (list == nullptr) {
      recording_.notes += stem + " (no command list); ";
      return;
    }
    constexpr D3D12_RESOURCE_STATES COMMON = D3D12_RESOURCE_STATE_COMMON;
    std::vector<uint8_t> chunks;
    std::vector<Readback> inputs;
    if (recording_.inputs) {
      AddChunk(&chunks, recording::CHUNK_BLOCK, "block", std::as_bytes(std::span(&shared, 1)).first(dlaa_bridge::SharedBytes(version)));
      if (!cluster_data.empty()) AddChunk(&chunks, recording::CHUNK_SETS, "sets", std::as_bytes(cluster_data));
      if (request == 1 && recorded_vertex_end_ != 0) {
        CopyToReadback(list, "vertices", vertices_.Get(), COMMON, &inputs, 0, 0,
                       (recorded_vertex_end_ + dlaa_bridge::RT_TRANSFER_WIDTH - 1) / dlaa_bridge::RT_TRANSFER_WIDTH);
      }
      if (request == 1 && atlas_) CopyToReadback(list, "atlas", atlas_.Get(), COMMON, &inputs);
      for (const auto& [name, input] : std::initializer_list<std::pair<const char*, ID3D12Resource*>>{
               {"positions", positions_.Get()}, {"sun", sun_.Get()}, {"light", light_.Get()}, {"color", previous_color_.Get()},
               {"normals", guide_normals_.Get()}, {"albedo", guide_albedo_.Get()}, {"roughness", guide_roughness_.Get()}}) {
        if (input != nullptr) CopyToReadback(list, name, input, COMMON, &inputs);
      }
    }
    std::vector<Readback> outputs;
    for (const RecordedResource& resource : RecordedResources()) {
      if ((resource.written_by & (1u << request)) != 0 && *resource.member && (resource.output || (recording_.histories && frame == recording_.last))) {
        CopyToReadback(list, resource.name, resource.member->Get(), resource.state, &outputs, resource.offset, resource.bytes);
      }
    }
    std::vector<Readback> digest_reads;
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> descs;
    std::unordered_map<uint64_t, uint64_t> blas_names;
    std::vector<std::array<uint64_t, 4>> scene;
    if (request == 1) {
      const Slot& slot = slots_[recorded_scene_.slot];
      descs.assign(slot.instance_data, slot.instance_data + recorded_scene_.cpu_instances);
      if (recorded_scene_.smoke) descs.push_back(slot.instance_data[MAX_INSTANCES - 1]);
      if (recorded_scene_.placed && recorded_scene_.tlas_instances > recorded_scene_.cpu_instances) {
        CopyToReadback(list, "placed", placed_instances_[placed_last_].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, &digest_reads,
                       uint64_t{recorded_scene_.cpu_instances} * sizeof(D3D12_RAYTRACING_INSTANCE_DESC),
                       uint64_t{recorded_scene_.tlas_instances - recorded_scene_.cpu_instances} * sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
      }
      CopyToReadback(list, "records", records_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, &digest_reads, 0, uint64_t{STATIC_RECORDS} * 16);
      for (const auto& [key, mesh] : meshes_) {
        blas_names[mesh.blas.address] = key;
        scene.push_back({key, (uint64_t{mesh.record} << 32) | mesh.triangles, (uint64_t{mesh.normal_offset} << 32) | mesh.uv_offset,
                         (uint64_t{mesh.first_instance} << 32) | (uint64_t{mesh.transforms.size()} << 8) | (mesh.placed ? 1u : 0u) | (mesh.foliage ? 2u : 0u)
                             | (mesh.micromaps ? 4u : 0u)});
      }
      for (const auto& [key, set] : sets_) {
        scene.push_back({key ^ 0x5E75E75E75E75E7ull, (uint64_t{set.offset} << 32) | set.clusters, (uint64_t{set.triangles} << 32) | set.vertices,
                         set.generation | (uint64_t{set.template_first} << 40) | (set.micromaps ? (1ull << 39) : 0ull)});
      }
      for (size_t blas = 0; blas < CLUSTER_BLASES; ++blas) {
        if (cluster_blases_[blas]) blas_names[cluster_blases_[blas]->GetGPUVirtualAddress()] = 0xC1A5000000000000ull + blas;
      }
      if (units_blas_) blas_names[units_blas_->GetGPUVirtualAddress()] = 0x0417500000000000ull;
      if (smoke_blas_) blas_names[smoke_blas_->GetGPUVirtualAddress()] = 0x5303E00000000000ull;
    }
    const uint64_t value = SubmitRecorderList(list);
    QueueRecordingJob(value, [this, stem, frame = named, request, folder = recording_.folder, header = RecordingHeader(named, request), chunks = std::move(chunks),
                              inputs = std::move(inputs), outputs = std::move(outputs), digest_reads = std::move(digest_reads), descs = std::move(descs),
                              blas_names = std::move(blas_names), scene = std::move(scene)]() mutable {
      if (!chunks.empty() || !inputs.empty()) WriteRecordingFile(folder / ("request_" + stem + ".bin"), header, chunks, &inputs, true);
      WriteRecordingFile(folder / ("output_" + stem + ".bin"), header, {}, &outputs, false);
      if (request != 1) return;
      uint64_t records_hash = 0;
      for (Readback& read : digest_reads) {
        uint8_t* mapped = nullptr;
        if (FAILED(read.buffer->Map(0, nullptr, reinterpret_cast<void**>(&mapped)))) continue;
        if (read.name == "records") {
          records_hash = HashBytes(std::as_bytes(std::span(mapped, read.bytes)));
        } else {
          const auto* placed = reinterpret_cast<const D3D12_RAYTRACING_INSTANCE_DESC*>(mapped);
          descs.insert(descs.end(), placed, placed + (read.bytes / sizeof(D3D12_RAYTRACING_INSTANCE_DESC)));
        }
        const D3D12_RANGE none = {};
        read.buffer->Unmap(0, &none);
        read.buffer.Reset();
      }
      // Each desc by its BLAS's name (an unknown address all ones), order-free.
      std::vector<uint64_t> named;
      for (D3D12_RAYTRACING_INSTANCE_DESC desc : descs) {
        const auto name = blas_names.find(desc.AccelerationStructure);
        desc.AccelerationStructure = (name != blas_names.end()) ? name->second : ((desc.AccelerationStructure == 0) ? 0u : UINT64_MAX);
        named.push_back(HashBytes(std::as_bytes(std::span(&desc, 1))));
      }
      std::ranges::sort(named);
      std::ranges::sort(scene);
      std::ofstream(folder / std::format("digest_{}.txt", frame))
          << std::format("frame {}\ninstances {} {:016X}\nrecords {:016X}\nscene {} {:016X}\n", frame, named.size(), HashBytes(std::as_bytes(std::span(named))),
                         records_hash, scene.size(), HashBytes(std::as_bytes(std::span(scene))));
    });
  }
#endif

  ComPtr<ID3D12Device5> device_;
  bool dxr12_ = false;
  std::string sdk_;
  ComPtr<ID3D12CommandQueue> queue_;
  std::array<Slot, SLOTS> slots_;
  size_t next_slot_ = 0;
  ComPtr<ID3D12Fence> own_fence_;
  uint64_t own_value_ = 0;
  uint64_t trace_value_ = 0;  // own_fence_'s value after the last trace
  HANDLE event_ = nullptr;
  ComPtr<ID3D12Resource> vertex_buffer_;
  ComPtr<ID3D12Resource> scratch_;
  uint64_t scratch_bytes_ = 0;
  ComPtr<ID3D12Resource> tlas_;
  ComPtr<ID3D12Resource> tlas_scratch_;
  ComPtr<ID3D12Resource> records_;
  ComPtr<ID3D12Resource> uvs_;
  ComPtr<ID3D12Resource> normals_;
  // Placed meshes (v14): their build rows (three texels a record), the instances the placement dispatches write in turn
  // (the last one written), their counters and dedupe table (rt_place's u8), the dispatch.
  ComPtr<ID3D12Resource> build_rows_;
  ComPtr<ID3D12Resource> left_out_start_;
  LeftOut left_out_;
  // The leaves' opacity micromaps (NVIDIA's OMM SDK; none without omm-lib.dll), on their way: read back by a request in
  // flight, read (waiting for the bake thread), on it, baked (waiting for a request to build them).
  bool bake_micromaps_ = false;
  std::vector<std::unique_ptr<MicromapBake>> micromap_reads_;
  std::vector<std::unique_ptr<MicromapBake>> micromap_queue_;
  std::array<std::future<std::unique_ptr<MicromapBake>>, MICROMAP_WORKERS> micromap_baking_;
  std::vector<std::unique_ptr<MicromapBake>> micromaps_baked_;
  // Since the last log line: meshes baked, their data's bytes, failed, of them grass sets (their templates rebuilt).
  std::array<uint64_t, 4> micromap_tally_ = {};
  bool cluster_micromaps_ = false;  // Mega Geometry's operations allow micromaps (grass sets' templates)
  uint64_t set_generation_ = 0;
  uint64_t micromap_logged_ = 0;                 // that line's time (GetTickCount64)
  // The bakes' finest micromap levels: grass's, leaves' (the player's RENODX_TW_PLAY_MICROMAP_LEVEL and
  // RENODX_TW_PLAY_LEAF_MICROMAP_LEVEL set them for its bakes, LoadRecordingState).
  int grass_micromap_level_ = GRASS_MICROMAP_LEVEL;
  int leaf_micromap_level_ = 6;
#if defined(RENODX_OMM_SDK)
  OmmSdk omm_;
  OmmGpu omm_gpu_;
#endif
  bool gpu_micromaps_ = false;  // GPU Micromap Baking ready (MakeGpuBaker)
  ComPtr<ID3D12InfoQueue> info_queue_;  // RENODX_TW_D3D12_DEBUG
  HRESULT debug_result_ = S_FALSE;
  // For the log: the device's start, a slow request's report (GPU over SLOW_MS; at most SLOW_REPORTS), a garbage
  // triangle's (one in 10 s).
  std::string slow_report_;
  uint32_t slow_reports_ = 0;
  std::string garbage_example_;
  uint64_t garbage_example_time_ = 0;
  std::array<ComPtr<ID3D12Resource>, 2> placed_instances_;
  ComPtr<ID3D12Resource> smoke_tlas_;  // the smoke's TLAS (SmokeTransmittance), its one instance
  ComPtr<ID3D12Resource> smoke_tlas_scratch_;
  uint32_t placed_last_ = 0;
  ComPtr<ID3D12Resource> placed_state_;
  ComPtr<ID3D12Resource> placed_scratch_;
  uint32_t placed_capacity_ = MAX_INSTANCES;  // their room now (PlacedRoom)
  uint32_t placed_limit_ = MAX_INSTANCES;     // the most (by the GPU's memory)
  uint32_t placed_written_ = 0;               // the last trace's, fitting or not (rt_place's counter)
  bool placed_rule_ = false;                  // One Model per Place as the last trace had it
  uint32_t placed_before_rule_ = 0;           // placed_written_ when it changed
  uint32_t placed_rule_traces_ = 0;           // traces until the log line after a change (0: said)
  size_t placed_slot_ = SLOTS;                // whose readback holds the counter of the last trace (SLOTS: read already)
  std::future<std::unique_ptr<PlacedRoom>> placed_growing_;
  std::string room_note_;  // the status's: a room that found no memory
  ComPtr<ID3D12PipelineState> place_pipeline_;
  // The units' BLAS (refitted in place), the vertex counts of its draws at its last build, refits since.
  ComPtr<ID3D12Resource> units_blas_;
  uint64_t units_bytes_ = 0;
  // The smoke's BLAS (built each request; Mega Geometry's in cluster_blases_[3]).
  ComPtr<ID3D12Resource> smoke_blas_;
  uint64_t smoke_bytes_ = 0;
  bool clusters_ = false;  // Mega Geometry's cluster operations for the units
  uint32_t cluster_limit_ = MAX_CLUSTERS;  // the most a frame (CLUSTER_TIERS)
  uint32_t cluster_room_ = MAX_CLUSTERS;   // room for them now (ClusterRoom: grown while a frame fills 3/4 of it)
  bool clusters_crowded_ = false;          // the last request's clusters filled 3/4 of a run's room or more
  // The driver refused the cluster BLASes' batched build: one call each (RENODX_TW_TEST_CLUSTER_BLASES_APART: from the
  // start, the GPU harness's check of that path).
  bool cluster_blases_apart_ = GetEnvironmentVariableW(L"RENODX_TW_TEST_CLUSTER_BLASES_APART", nullptr, 0) != 0;
  // Record's refused Mega Geometry calls already in the log: templates 1, CLAS instantiate 2, CLAS build 4, a BLAS 8.
  uint32_t cluster_refusals_said_ = 0;
  uint64_t templated_wanted_ = 0;          // and the clusters its runs wanted (fitting or not)
  uint64_t built_wanted_ = 0;
  uint32_t built_limit_ = MAX_CLUSTERS;    // of the room built from triangles (the rest instantiated from templates)
  uint64_t cluster_bytes_ = 0;             // the room's CLASes, cluster BLAS, scratch and arguments (the status)
  uint32_t dynamic_uvs_ = DYNAMIC_UVS;     // a request's alpha-tested corners (by the GPU's memory)
  uint32_t uv_pool_ = STATIC_UVS + (SLOTS * DYNAMIC_UVS);
#if defined(RENODX_NVAPI_CLUSTERS)
  NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS clas_inputs_ = {};
  NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS cluster_blas_inputs_ = {};
  NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS template_inputs_ = {};
  NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS instantiate_inputs_ = {};
  std::future<std::unique_ptr<ClusterRoom>> cluster_growing_;  // a bigger room being made (a worker's)
#endif
  ComPtr<ID3D12Resource> clas_;
  ComPtr<ID3D12Resource> clas_addresses_;
  std::array<ComPtr<ID3D12Resource>, CLUSTER_BLASES> cluster_blases_;  // CLUSTER_BLASES' (the units' at first, cluster_destination_)
  ComPtr<ID3D12Resource> cluster_destination_;
  ComPtr<ID3D12Resource> cluster_scratch_;
  ComPtr<ID3D12Resource> cluster_indices_;
  bool cluster_indices_written_ = false;
  // The CLASes' arguments a request builds (the compact pass's, then the captures' copied from the slot), the compact
  // draws' cluster sets by key in their pool, its free units, the times a full pool dropped sets (CompactState) and the
  // compact pass.
  ComPtr<ID3D12Resource> cluster_args_;
  ComPtr<ID3D12Resource> set_pool_;
  // The cluster templates (TEMPLATE_POOL_BYTES, template_bytes_ each, a set's together), a request's builds' places, the
  // instantiated CLASes' share of clas_.
  ComPtr<ID3D12Resource> templates_;
  ComPtr<ID3D12Resource> template_destinations_;
  // Zero geometry indices for a template's triangles: an instantiation's geometry index offset is only applied to a
  // template built with them (without, the instantiated triangles' geometry index, their record, came out wrong).
  ComPtr<ID3D12Resource> template_geometry_;
  uint32_t template_bytes_ = 0;
  uint64_t instantiated_bytes_ = 0;
  Ranges free_templates_;
  std::unordered_map<uint64_t, ClusterSet> sets_;
  Ranges free_set_units_;
  std::vector<PendingSetFree> pending_set_frees_;
  uint32_t set_drops_ = 0;
  ComPtr<ID3D12PipelineState> compact_pipeline_;
  // Swaying Shadows Range / Off-Screen Sway (rt_sway.cs_6_5.hlsl; null: unavailable): the kept clusters' arguments
  // (cluster_args_'s size, resting in NON_PIXEL_SHADER_RESOURCE), the cluster BLASes' lists (clas_addresses_'s size, in
  // UNORDERED_ACCESS), their arguments and the CLAS counts (SWAY_COUNTS_BYTES, resting in NON_PIXEL_SHADER_RESOURCE).
  ComPtr<ID3D12PipelineState> sway_pipeline_;
  ComPtr<ID3D12Resource> sway_arguments_;
  ComPtr<ID3D12Resource> sway_lists_;
  ComPtr<ID3D12Resource> sway_counts_;
  // The partitioned TLAS (null: the TLAS is built each frame) and each of its instances as last written (its BLAS, 0
  // inactive; its InstanceID and flags), built once before the first update.
#if defined(RENODX_NVAPI_CLUSTERS)
  NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_INPUTS ptlas_inputs_ = {};
#endif
  ID3D12Resource* ptlas_ = nullptr;  // the last built of ptlas_buffers_
  std::array<ComPtr<ID3D12Resource>, 2> ptlas_buffers_;
  uint32_t ptlas_current_ = 0;
  ComPtr<ID3D12Resource> ptlas_scratch_;
  struct PtlasInstance {
    uint64_t blas = 0;
    uint32_t id = 0;
    uint32_t flags = UINT32_MAX;  // never written
    std::array<float, 12> transform = {};
    uint32_t stamp = 0;  // the request it was wanted in (ptlas_wanted_)
  };
  std::vector<PtlasInstance> ptlas_written_;
  std::vector<PtlasInstance> ptlas_wanted_;
  std::vector<uint32_t> ptlas_places_;  // last request's
  uint32_t ptlas_stamp_ = 0;
  bool ptlas_built_ = false;
  std::vector<uint32_t> units_layout_;
  uint32_t units_refits_ = 0;
  uint32_t units_record_ = 0;
  bool units_masked_ = false;  // grass among them
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12PipelineState> trace_pipeline_;
  ComPtr<ID3D12PipelineState> soft_pipeline_;
#if defined(RENODX_NRD)
  NrdSignal nrd_sun_;
  NrdSignal nrd_ambient_;
  NrdSignal nrd_bounce_;
  NrdSignal nrd_reflections_;
  ComPtr<ID3D12Resource> nrd_bounce_hit_distance_;
  std::array<float, 3> sun_toward_ = {};  // the last sun direction read back (Slot::sun_readback; 0: none yet)
#endif
  ComPtr<ID3D12PipelineState> scene_data_pipeline_;
  ComPtr<ID3D12DescriptorHeap> descriptors_;
  UINT descriptor_size_ = 0;
  Ranges free_triangles_;
  Ranges free_uvs_;
  Ranges free_instances_;  // the instanced meshes' places, STATIC_RECORDS to MAX_INSTANCES
  std::vector<uint32_t> free_records_;
  std::vector<PendingFree> pending_frees_;
  // The addon's.
  ComPtr<ID3D12Resource> vertices_;
  ComPtr<ID3D12Resource> positions_;
  ComPtr<ID3D12Resource> sun_;
  ComPtr<ID3D12Resource> visibility_;
  ComPtr<ID3D12Resource> ambient_;
  ComPtr<ID3D12Resource> atlas_;
  ComPtr<ID3D12Resource> light_;   // the ray-traced Light Bounce's prepared light (v7)
  ComPtr<ID3D12Resource> bounce_;  // its output
  ComPtr<ID3D12Resource> light_mask_;  // the local lights' lit bits (v9)
  ComPtr<ID3D12Resource> fog_;         // the fog shadows (v11)
  ComPtr<ID3D12Resource> fog_lights_;  // the local lights' light in the fog (v18)
  ComPtr<ID3D12Resource> tint_;        // Coloured Shadows' filter (v19)
  // The ambient visibility kept over frames (v11): its two sets, the last trace's positions, camera, frame and set.
  std::array<ComPtr<ID3D12Resource>, 2> ambient_history_;
  ComPtr<ID3D12Resource> ambient_positions_;
  std::array<float, 16> ambient_view_projection_ = {};
  uint32_t ambient_frame_ = UINT32_MAX;
  uint32_t ambient_parity_ = 0;
  // The local lights' bits kept over frames (HISTORY_LIGHTS), the last trace that wrote them and its lights.
  std::array<ComPtr<ID3D12Resource>, 2> light_history_;
  uint32_t lights_frame_ = UINT32_MAX;
  uint32_t lights_mode_traced_ = 0;  // the last kept lights' bits' Torch and Fire Light in the Light Bounce mode (0 none, 1, 3 the ratio)
  std::array<std::array<float, 4>, dlaa_bridge::RT_ALL_LIGHTS> previous_lights_ = {};
  uint32_t previous_light_count_ = 0;
  ComPtr<ID3D12PipelineState> fog_pipeline_;
  ComPtr<ID3D12PipelineState> lights_pipeline_;
  // Torch and Fire Light in the Light Bounce (TORCHES_IN_BOUNCE builds): the local lights' trace, the reuse.
  ComPtr<ID3D12PipelineState> lights_bounce_pipeline_;
  ComPtr<ID3D12PipelineState> reuse_lights_pipeline_;
  ComPtr<ID3D12PipelineState> bounce_pipeline_;
  // Paths (v8): last frame's colour (the DLAA input), the G-buffer guides, the reflections' outputs; the history and the
  // cache of the helper's own; the pipelines.
  ComPtr<ID3D12Resource> previous_color_;
  ComPtr<ID3D12Resource> guide_normals_;
  ComPtr<ID3D12Resource> guide_albedo_;
  ComPtr<ID3D12Resource> guide_roughness_;
  ComPtr<ID3D12Resource> reflection_;
  ComPtr<ID3D12Resource> specular_;
  ComPtr<ID3D12Resource> hit_distance_;  // Ray Reconstruction's specular hit distance (v17; u31)
  // Last frame's positions and this frame's for the next (t11), a slice each: copied_slice_ took the last copy,
  // previous_positions_slice_ is what this frame's requests read.
  ComPtr<ID3D12Resource> kept_positions_;
  uint32_t copied_slice_ = 0;
  uint32_t previous_positions_slice_ = 0;
  // The slice the last trace's copy is in (one copy for the trace's history and the paths': rt_camera.hlsli
  // PATH_AMBIENT_KEPT), NO_SLICE: in ambient_positions_.
  static constexpr uint32_t NO_SLICE = UINT32_MAX;
  uint32_t trace_positions_slice_ = NO_SLICE;
  // Softer Water Reflections: the positions at the reflections request of frame water_frame_ (the water's surface).
  ComPtr<ID3D12Resource> water_positions_;
  uint32_t water_frame_ = UINT32_MAX;
  ComPtr<ID3D12Resource> specular_accumulated_;
  ComPtr<ID3D12Resource> specular_history_;
  ComPtr<ID3D12Resource> specular_meta_;
  ComPtr<ID3D12Resource> specular_meta_history_;
  ComPtr<ID3D12Resource> cache_keys_;
  ComPtr<ID3D12Resource> cache_radiance_;
  ComPtr<ID3D12Resource> cache_sums_;
  ComPtr<ID3D12Resource> cache_albedo_;
  ComPtr<ID3D12Resource> cache_owners_;
  ComPtr<ID3D12Resource> light_fit_;
  ComPtr<ID3D12Resource> light_kept_;
  uint32_t path_ambient_frame_ = UINT32_MAX;  // the last frame the paths took the sky for the game's ambient
  bool path_kept_cube_ = false;               // RT_PATH_KEPT_CUBE of that frame's request (the reflections' requests don't carry it)
  bool path_sun_light_ = false;               // RT_PATH_SUN_LIGHT of that frame's request
  ComPtr<ID3D12Resource> kept_readback_;      // light_kept_ at the last freeze, for the log
  bool kept_copy_wanted_ = false;
  uint32_t kept_copy_frame_ = UINT32_MAX;
  ComPtr<ID3D12PipelineState> path_pipeline_;
  ComPtr<ID3D12PipelineState> path_one_pipeline_;  // its build for one ray a texel (PATH_ONE_SAMPLE)
#if defined(RENODX_PATH_PROBE)
  ComPtr<ID3D12PipelineState> path_probe_pipeline_;  // RENODX_TW_PATH_PROBE's (tests)
#endif
  ComPtr<ID3D12PipelineState> reflect_pipeline_;
  ComPtr<ID3D12PipelineState> reflect_one_pipeline_;  // its build for one sample a pixel (REFLECT_ONE_SAMPLE)
  // Path Tracing SER (DXR 1.2; null: unavailable): the paths' ray generation shaders, the general build's and the one of
  // one ray a texel's (PATH_ONE_SAMPLE), their records (at 0 and at PATH_SER_RECORD).
  ComPtr<ID3D12StateObject> path_ser_;
  ComPtr<ID3D12StateObject> path_ser_one_;
  ComPtr<ID3D12Resource> path_ser_table_;
  static constexpr uint64_t PATH_SER_RECORD = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
  // SHaRC Radiance Cache (rt_sharc.hlsli; null pipelines: unavailable): the paths' SHaRC variants (the general one and
  // the one of one ray a texel, PATH_ONE_SAMPLE), its update and resolve,
  // its buffers (SHARC_ENTRIES each of SHARC_STRIDES: the keys, a frame's sums, the resolved light; made at first use,
  // zeroed as committed buffers are, kept while the paths use it) and the frame its update last ran.
  ComPtr<ID3D12PipelineState> sharc_path_pipeline_;
  ComPtr<ID3D12PipelineState> sharc_path_one_pipeline_;
  ComPtr<ID3D12PipelineState> sharc_update_pipeline_;
  ComPtr<ID3D12PipelineState> sharc_resolve_pipeline_;
  std::array<ComPtr<ID3D12Resource>, 3> sharc_buffers_;
  uint32_t sharc_frame_ = UINT32_MAX;
  std::string sharc_failed_;  // its pipeline that failed
  static constexpr uint32_t SHARC_ENTRIES = 1u << 21;  // rt_sharc.hlsli SHARC_CAPACITY
  static constexpr std::array<uint64_t, 3> SHARC_STRIDES = {8, 16, 16};
  static constexpr uint32_t SHARC_BLOCK = 5;  // rt_sharc_update.hlsl's
  ComPtr<ID3D12PipelineState> reuse_pipeline_;  // ReSTIR GI's spatial reuse
  ComPtr<ID3D12PipelineState> reuse_lean_pipeline_;  // its Lean Reuse build (REUSE_LEAN)
  ComPtr<ID3D12PipelineState> reuse_near_pipeline_;  // its Near Reuse build (REUSE_NEAR)
  ComPtr<ID3D12PipelineState> reuse_compatible_pipeline_;           // its Matched ReSTIR Neighbours build (REUSE_COMPATIBLE; RTXDI builds)
  ComPtr<ID3D12PipelineState> reuse_compatible_balanced_pipeline_;  // that build's Balanced ReSTIR Light twin
  ComPtr<ID3D12PipelineState> reuse_compatible_balanced_raytraced_pipeline_;  // and its Raytraced twin
  // Balanced ReSTIR Light's twins of the usual reuse builds: each plain build, its REUSE_BALANCED twin's code (G1 Basic) and
  // its REUSE_BALANCED_RAYTRACED twin's (G1 Raytraced) (DXR 1.1, DXR 1.2), in reuse_balanced_pipelines_'s order.
  struct BalancedTwin {
    ComPtr<ID3D12PipelineState> RtScene12::* plain;
    std::span<const unsigned char> code;
    std::span<const unsigned char> code_omm;
    std::span<const unsigned char> code_raytraced;
    std::span<const unsigned char> code_raytraced_omm;
  };
  static constexpr std::array<BalancedTwin, 4> BalancedTwins() {
    return {{
        {&RtScene12::reuse_pipeline_, RT_REUSE_BALANCED, RT_REUSE_BALANCED_OMM, RT_REUSE_BALANCED_RAYTRACED, RT_REUSE_BALANCED_RAYTRACED_OMM},
        {&RtScene12::reuse_lights_pipeline_, RT_REUSE_LIGHTS_BALANCED, RT_REUSE_LIGHTS_BALANCED_OMM, RT_REUSE_LIGHTS_BALANCED_RAYTRACED,
         RT_REUSE_LIGHTS_BALANCED_RAYTRACED_OMM},
        {&RtScene12::reuse_lean_pipeline_, RT_REUSE_LEAN_BALANCED, RT_REUSE_LEAN_BALANCED_OMM, RT_REUSE_LEAN_BALANCED_RAYTRACED,
         RT_REUSE_LEAN_BALANCED_RAYTRACED_OMM},
        {&RtScene12::reuse_near_pipeline_, RT_REUSE_NEAR_BALANCED, RT_REUSE_NEAR_BALANCED_OMM, RT_REUSE_NEAR_BALANCED_RAYTRACED,
         RT_REUSE_NEAR_BALANCED_RAYTRACED_OMM},
    }};
  }
  std::array<ComPtr<ID3D12PipelineState>, 4> reuse_balanced_pipelines_;
  std::array<ComPtr<ID3D12PipelineState>, 4> reuse_balanced_raytraced_pipelines_;
  uint32_t balanced_logged_ = 0;  // the Balanced ReSTIR Light mode at its last log line (0 none, 1 Basic, 2 Raytraced)
  ComPtr<ID3D12PipelineState> path_tests_pipeline_;   // the paths' build with ReSTIR GI's test switches (REUSE_TESTS)
  ComPtr<ID3D12PipelineState> reuse_tests_pipeline_;  // the reuse's (REUSE_TESTS)
  ComPtr<ID3D12PipelineState> reuse_boil_pipeline_;  // its boiling filter (REUSE_BOIL; RTXDI builds)
  // Torch Light in Path-Traced GI (PT DI step 3; RTXDI builds): the ReGIR build, its cells (u76), rt_lights' torch light at
  // the render pixels (u75, made while the row asked within HISTORY_FRAMES), the last paths request that asked and
  // the last trace that wrote it.
  ComPtr<ID3D12PipelineState> regir_pipeline_;
  std::array<ComPtr<ID3D12PipelineState>, 2> lights_torch_pipelines_;  // rt_lights' and rt_lights_bounce's twins with T1
  ComPtr<ID3D12PipelineState> sharc_update_regir_pipeline_;            // rt_sharc_update's twin with T2 (SHaRC builds)
  ComPtr<ID3D12Resource> regir_cells_;
  ComPtr<ID3D12Resource> torch_irradiance_;
  uint32_t torch_light_frame_ = UINT32_MAX;
  uint32_t torch_irradiance_frame_ = UINT32_MAX;
  ComPtr<ID3D12PipelineState> reuse_rtxdi_pack_pipeline_;  // the RTXDI GI contest's pack (REUSE_RTXDI_PACK; the reference build)
  ComPtr<ID3D12PipelineState> reuse_rtxdi_pipeline_;       // its reuse (REUSE_RTXDI)
  ComPtr<ID3D12Resource> rtxdi_reservoirs_;  // its reservoirs (RTXDI_GI_RESERVOIRS)
  ComPtr<ID3D12Resource> rtxdi_offsets_;     // its neighbour offsets (RTXDI_OFFSETS)
  // ReSTIR GI's and Own ReSTIR's reservoir formats (rt_path.hlsli ReservoirPoint / ReservoirData; G4: RT_RESERVOIR_COMPACT).
  static constexpr DXGI_FORMAT RESERVOIR_POINTS = (RT_RESERVOIR_COMPACT ? DXGI_FORMAT_R32G32B32A32_UINT : DXGI_FORMAT_R32G32B32A32_FLOAT);
  static constexpr DXGI_FORMAT RESERVOIR_DATA = (RT_RESERVOIR_COMPACT ? DXGI_FORMAT_R32G32_UINT : DXGI_FORMAT_R32G32B32A32_UINT);
  std::array<ComPtr<ID3D12Resource>, 2> reuse_samples_;
  std::array<ComPtr<ID3D12Resource>, 2> reuse_data_;
  std::array<ComPtr<ID3D12Resource>, 2> reuse_surface_;  // the bounce texels' rays' origins and normals (SURFACE_ORIGIN, SURFACE_NORMAL)
  uint32_t reuse_frame_ = UINT32_MAX;  // the frame the reservoirs were last made in
  uint32_t reuse_lights_mode_ = 0;     // its Torch and Fire Light in the Light Bounce mode (rt_camera.hlsli LIGHTS_MODE; 0: none)
  uint32_t reuse_parity_ = 0;          // this frame's set
  std::array<uint32_t, 3> reuse_size_ = {};  // the bounce's size and scale they are at
  std::array<ComPtr<ID3D12Resource>, 2> own_sky_;  // Path-Traced Ambient's own sky at the bounce texels (rt_path.hlsli ScreenSky)
  uint32_t sky_frame_ = UINT32_MAX;          // the frame the paths last wrote it in
  uint32_t sky_parity_ = 0;                  // the set the next paths write
  std::array<uint32_t, 3> sky_size_ = {};    // the bounce's size and scale it is at
  // Torch and Fire Shadows from the Light Bounce (rt_path.hlsli ReuseTexel, rt_lights): the ratio's two sets at the bounce
  // texels, the set the next bounce writes, the frame and texels of the last.
  std::array<ComPtr<ID3D12Resource>, 2> light_ratio_;
  uint32_t ratio_parity_ = 0;
  uint32_t ratio_frame_ = UINT32_MAX;
  std::array<uint32_t, 3> ratio_size_ = {};
  ComPtr<ID3D12PipelineState> reflect_reservoir_pipeline_;  // Own ReSTIR's reservoirs
  std::array<ComPtr<ID3D12Resource>, 2> reflect_samples_;
  std::array<ComPtr<ID3D12Resource>, 2> reflect_data_;
  uint32_t reflect_frame_ = UINT32_MAX;  // the frame they were last made in
  uint32_t reflect_parity_ = 0;          // this frame's set
  std::array<uint32_t, 2> reflect_size_ = {};  // the render size they are at
  ComPtr<ID3D12PipelineState> cache_fill_pipeline_;
  ComPtr<ID3D12PipelineState> cache_resolve_pipeline_;
  ComPtr<ID3D12PipelineState> reflect_filter_pipeline_;
  // GPU times (but with RENODX_TW_NO_GPU_TIMES): each slot's timestamps (a trace: its start, its TLAS, its trace, its end; paths:
  // their start and end), read when the slot comes round again; the last trace's and paths' phases in milliseconds.
  ComPtr<ID3D12QueryHeap> timestamps_;
  ComPtr<ID3D12Resource> timestamp_readback_;
  uint64_t timestamp_frequency_ = 0;
  std::array<bool, SLOTS> slot_traced_ = {};
  std::array<bool, SLOTS> slot_reflected_ = {};  // a path request's: the reflections (else the Light Bounce)
  std::array<bool, SLOTS> slot_clustered_ = {};  // a trace's: its clusters built (timed by the paths' stamps)
  std::array<uint32_t, 2> paths_age_ = {};  // the sun traces since the Light Bounce's, the reflections' last times
  // The scene (captures, BLASes), the TLAS, the trace; the Light Bounce, the reflections; the scene's cluster build.
  std::array<double, 6> gpu_ms_ = {};
  std::array<float, 16> current_view_projection_ = {};
  std::array<float, 16> previous_view_projection_ = {};
  uint32_t paths_frame_ = UINT32_MAX;     // the frame of the last path request (the history copied then)
  uint32_t copied_frame_ = UINT32_MAX;    // the frame kept_positions_' copied_slice_ holds (the trace's or the paths' copy)
  uint32_t paths_copied_frame_ = UINT32_MAX;  // the last frame the paths took a copy (their own or the trace's): their history
  uint32_t light_frame_ = UINT32_MAX;     // the frame the prepared light is from
  uint32_t cache_frame_ = UINT32_MAX;     // the frame the cache was last filled
  uint32_t specular_frame_ = UINT32_MAX;  // the frame of the last reflections
  uint32_t sun_traced_frame_ = UINT32_MAX;
  bool history_valid_ = false;
  bool cache_filled_ = false;
  bool specular_history_valid_ = false;
  bool traced_ = false;  // a TLAS was built (the bounce traces it)
  std::array<uint32_t, HISTORY_KINDS> history_used_ = {};  // the frame each history was last used in
  ComPtr<ID3D12Resource> sun_rays_;  // the helper's, the textures' size
  ComPtr<ID3D12Fence> fence_;
  std::unordered_map<uint64_t, Mesh> meshes_;
  uint64_t builds_ = 0;
  std::vector<Release> releases_;
  // The static BLASes' pools (BlasPlace): the builds' and the compacted ones', each its chunks.
  struct BlasChunk {
    ComPtr<ID3D12Resource> buffer;
    Ranges free;
  };
  std::array<std::vector<BlasChunk>, 2> blas_pools_;
  size_t blas_chunks_logged_ = 0;  // their chunks at the last log line
  bool failed_ = false;
  std::atomic_bool request_failed_ = false;  // Lost (the worker sets it, main.cpp reads it)
  std::atomic_bool timed_out_ = false;       // TimedOut (likewise)
  Stall stall_;                              // LastStall (the worker's, read after its drain)
  bool details_ = false;  // the addon's Log Details or profile (this request's): GPU times, markers, the status, the log's lines
  void NoteNrd(uint32_t signal, bool ran) {
    if (ran) {
      nrd_off_.fetch_and(~signal);
    } else {
      nrd_off_.fetch_or(signal);
    }
  }

  std::atomic<uint32_t> nrd_off_ = 0;  // published from the helper's worker, read beside it
  std::string status_ = "Ray tracing off";
  // The last trace's times: the GPU's (after the status's label), the CPU's and the submit's (at its end).
  std::string gpu_timings_;
  std::string cpu_timings_;
  // The addon's ray tracing profile (RT_PROFILE, Record Ray Tracing Profile): each request's line into
  // tw_rt_profile_helper.csv in the helper's folder (made again at a recording's first trace), the last Record's counts
  // (PROFILE_COUNTS fields).
  static constexpr size_t PROFILE_COUNTS = 17;
  std::filesystem::path folder_;
  std::ofstream profile_;
  bool profiling_ = false;
  // A recording's trace being recorded: its passes' marks and when the CPU reached them.
  std::vector<std::pair<const wchar_t*, LARGE_INTEGER>> cpu_marks_;
  std::string profile_counts_;
#if defined(RENODX_RECORDER)
  // The recorder's (last: destroyed first, its writer waited for in the destructor). The journal while RENODX_TW_RECORD was
  // set at Initialize: the static meshes' vertices, their micromap bakes and the cluster sets', by key.
  bool journal_on_ = false;
  bool player_ = false;            // a play-back's scene (PlayRecording)
  uint64_t snapshot_waited_ = 0;   // own_value_ when a snapshot last waited for the bakes in flight (0: none)
#if defined(RENODX_NRD)
  // A play-back's NRD denoisers as recorded (NrdSignals' order; EnsureNrd's denoiser takes its one): their textures' size,
  // last frame, history (the permanent pool's textures' chunks).
  struct RecordedNrd {
    std::array<uint32_t, 2> size = {};
    Nrd12::Kept kept;
    std::vector<std::vector<uint8_t>> history;
  };
  std::array<std::optional<RecordedNrd>, 4> recorded_nrd_;
#endif
  std::unordered_map<uint64_t, Journal> journal_;
  std::unordered_map<uint64_t, Journal> micromap_journal_;
  std::unordered_map<uint64_t, Journal> set_journal_;
  ComPtr<ID3D12CommandAllocator> recorder_allocator_;
  ComPtr<ID3D12GraphicsCommandList4> recorder_list_;
  uint64_t recorder_value_ = 0;  // own_fence_'s value after its last list
  Recording recording_;
  std::future<void> recording_writer_;  // its last job (each waits for the one before)
  uint32_t recorded_vertex_end_ = 0;    // the last trace's vertices (Record)
  RecordedScene recorded_scene_;
  Playback playback_;  // the player's loaded recording (LoadPlayback)
  // The resident's (rt_serve.h): its timestamps (ServeTiming), the list they are in and its first, the burn (SetBurn), the
  // base build's pipelines of swapped stems (SwapStem), the moments (StartMoments).
  ServeTimingMode serve_timing_ = SERVE_TIMING_OFF;
  ComPtr<ID3D12QueryHeap> serve_stamps_;
  ComPtr<ID3D12Resource> serve_readback_;
  std::vector<ServeMarkEntry> serve_marks_;
  ID3D12GraphicsCommandList* serve_list_ = nullptr;
  uint32_t serve_list_first_ = 0;
  ComPtr<ID3D12PipelineState> burn_pipeline_;
  std::wstring burn_pass_;
  uint32_t burn_loops_ = 0;
  uint32_t burn_runs_ = 0;
  std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> base_pipelines_;
  Moments moments_;
  // The resource-use log's (StartUses): the root signature's ranges and parameters, each pipeline's DXIL (KeepCode), the
  // log, its folder, file and the stems' DXIL written there.
  std::array<D3D12_DESCRIPTOR_RANGE, 6> use_ranges_ = {};
  std::array<D3D12_ROOT_PARAMETER, 13> use_parameters_ = {};
  std::unordered_map<ID3D12PipelineState*, std::vector<uint8_t>> use_code_;
  std::unique_ptr<uses::Log<RtScene12>> uses_;
  std::filesystem::path use_folder_;
  std::ofstream use_file_;
  std::unordered_set<std::string> use_written_;
  std::map<D3D12_GPU_VIRTUAL_ADDRESS, std::pair<uint64_t, std::string>> use_unnamed_;  // unnamed buffers by address: size, name
#endif
};

}  // namespace twattila::dlaa_helper
