/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <format>
#include <shared_mutex>
#include <vector>

#include <include/reshade.hpp>

#include "../../utils/data.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "./buffer_writes.hpp"

// The game's Global Illumination while ours is on (Light Bounce, any method). Its probes do two jobs (DC860C60, their rays
// through the voxels: a miss brings the sky, t_specular_array x g_ambient_fudge_factor with alpha 1; a hit the voxel's lit
// colour x g_irradiance_power with alpha 0), and ours replaces only the second:
// - Sky Shade From the Game's Probes (the row, on): the hits' light goes (g_irradiance_power 0 in a copy of that dispatch's
//   global_illumination constants), the sky's light stays as the probes shade it under trees and roofs, for every one of
//   the 1019 readers. The lighting constants are not touched.
// - Off (v37, v38): the flag every reader takes, g_gi_enabled, goes to 0 where the game uploads it, and the readers take the
//   ambient cube unshaded (ATN 2026-11-10: forest floors twice as bright as the game's, fog and water too, which ours
//   never lit). lighting_VS_PS is 480 bytes in all 1228 shaders that have it, g_gi_enabled at 180 in all, and no other
//   constant buffer in the dump is 480 bytes (v37_cbuffer_sizes.py).
// The game's engine value gi_enabled is never written: its GI stage makes its textures only while the value is not 0 (at
// its construction and at every graphics change), and runs whenever it is not 0. Set to 0 and back in a battle, the
// stage ran without textures (ATN's crash 2026-11-06: a read of null + 0x18 at Warhammer3.exe+0x1A45803).
namespace twwarhammer3::game_gi {

constexpr uint64_t LIGHTING_BYTES = 480;
constexpr uint64_t FLAG_OFFSET = 180;
constexpr UINT PROBE_CONSTANTS_SLOT = 2;  // DC860C60's global_illumination (its RDEF)
constexpr UINT IRRADIANCE_POWER_OFFSET = 208;

static float sky_row = 1.f;  // Sky Shade From the Game's Probes
static std::atomic_bool off = false;       // ours on, the row off: the flag 0
static std::atomic_bool sky_only = false;  // ours on, the row on: the probes' hits unlit
static std::atomic_bool logged = false;
static std::atomic_bool sky_logged = false;
// The probes' rays' constants without the hits' light, the game's own while ours are bound.
static Microsoft::WRL::ComPtr<ID3D11Buffer> probe_constants;
static UINT probe_constants_bytes = 0;
static Microsoft::WRL::ComPtr<ID3D11Buffer> no_power;
static Microsoft::WRL::ComPtr<ID3D11Buffer> game_probe_constants;
// The game's lighting buffers with their open maps (buffer_writes.hpp).
static buffer_writes::Buffers lighting;
static auto& lighting_buffers = lighting.known;
static auto& open_map_count = lighting.open_map_count;

static void LogOnce(const char* how) {
  if (!logged.exchange(true, std::memory_order_relaxed)) {
    renodx::utils::log::i(std::format("[Warhammer III] First run: the game's Global Illumination flag off in its lighting constants ({}).", how));
  }
}

static void OnInitResource(reshade::api::device*, const reshade::api::resource_desc& desc, const reshade::api::subresource_data*,
                           reshade::api::resource_usage, reshade::api::resource resource) {
  buffer_writes::Add(&lighting, LIGHTING_BYTES, desc, resource);
}

static void OnDestroyResource(reshade::api::device*, reshade::api::resource resource) {
  buffer_writes::Remove(&lighting, resource);
}

static void OnMapBufferRegion(reshade::api::device*, reshade::api::resource resource, uint64_t offset, uint64_t, reshade::api::map_access access, void** data) {
  buffer_writes::Mapped(&lighting, off.load(std::memory_order_relaxed), resource, offset, access, data);
}

static void OnUnmapBufferRegion(reshade::api::device*, reshade::api::resource resource) {
  const bool written = buffer_writes::Unmapped(&lighting, resource, [](uint8_t* constants) {
    // Turned off since the map: the game's flag stays.
    if (!off.load(std::memory_order_relaxed)) return false;
    constexpr uint32_t GI_OFF = 0u;
    std::memcpy(constants + FLAG_OFFSET, &GI_OFF, sizeof(GI_OFF));
    return true;
  });
  if (written) {
    LogOnce("map");
  }
}

// The game's update, whole or past the flag, replaced by the same with the flag 0.
static bool UpdateWithFlagOff(ID3D11DeviceContext* native, const void* data, reshade::api::resource resource, uint64_t offset, uint64_t size) {
  if (!off.load(std::memory_order_relaxed)) return false;
  const bool sent = buffer_writes::Update<LIGHTING_BYTES>(lighting, native, data, resource, offset, size, FLAG_OFFSET + 4, [](uint8_t* constants) {
    constexpr uint32_t GI_OFF = 0u;
    std::memcpy(constants + FLAG_OFFSET, &GI_OFF, sizeof(GI_OFF));
    return true;
  });
  if (sent) {
    LogOnce("update");
  }
  return sent;
}

static bool OnUpdateBufferRegion(reshade::api::device* device, const void* data, reshade::api::resource resource, uint64_t offset, uint64_t size) {
  // Every update of every buffer passes by: the table first.
  if (!off.load(std::memory_order_relaxed) || !lighting.known.contains(resource.handle)) return false;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediate;
  reinterpret_cast<ID3D11Device*>(device->get_native())->GetImmediateContext(immediate.GetAddressOf());
  return UpdateWithFlagOff(immediate.Get(), data, resource, offset, size);
}

static bool OnUpdateBufferRegionCommand(reshade::api::command_list* cmd_list, const void* data, reshade::api::resource resource, uint64_t offset, uint64_t size) {
  return UpdateWithFlagOff(reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native()), data, resource, offset, size);
}

// The compute stage's global_illumination constants replaced by a copy without the hits' light, until GameProbeConstants.
inline void SkyOnlyProbeConstants(ID3D11DeviceContext* native) {
  Microsoft::WRL::ComPtr<ID3D11Buffer> game_constants;
  native->CSGetConstantBuffers(PROBE_CONSTANTS_SLOT, 1, game_constants.GetAddressOf());
  if (!game_constants) return;
  D3D11_BUFFER_DESC desc = {};
  game_constants->GetDesc(&desc);
  if (desc.ByteWidth < IRRADIANCE_POWER_OFFSET + sizeof(float)) return;
  if (probe_constants_bytes != desc.ByteWidth) {
    probe_constants.Reset();
    probe_constants_bytes = 0;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    native->GetDevice(device.GetAddressOf());
    const D3D11_BUFFER_DESC copy = {.ByteWidth = desc.ByteWidth, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    constexpr std::array<float, 4> ZEROS = {};
    const D3D11_BUFFER_DESC zeros = {.ByteWidth = sizeof(ZEROS), .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_SUBRESOURCE_DATA zeros_data = {.pSysMem = ZEROS.data()};
    if (FAILED(device->CreateBuffer(&copy, nullptr, probe_constants.GetAddressOf()))
        || FAILED(device->CreateBuffer(&zeros, &zeros_data, no_power.ReleaseAndGetAddressOf()))) {
      probe_constants.Reset();
      return;
    }
    probe_constants_bytes = desc.ByteWidth;
  }
  // A constant buffer takes no update of a part: the game's whole, then the one float from a buffer of zeros.
  native->CopyResource(probe_constants.Get(), game_constants.Get());
  const D3D11_BOX power = {.left = 0, .top = 0, .front = 0, .right = sizeof(float), .bottom = 1, .back = 1};
  native->CopySubresourceRegion(probe_constants.Get(), 0, IRRADIANCE_POWER_OFFSET, 0, 0, no_power.Get(), 0, &power);
  native->CSSetConstantBuffers(PROBE_CONSTANTS_SLOT, 1, probe_constants.GetAddressOf());
  game_probe_constants = std::move(game_constants);
  if (!sky_logged.exchange(true, std::memory_order_relaxed)) {
    renodx::utils::log::i("[Warhammer III] First run: the game's light probes bring the sky's light alone (cs DC860C60, their hits unlit).");
  }
}

// The game's own constants back where SkyOnlyProbeConstants put the copy.
inline void GameProbeConstants(ID3D11DeviceContext* native) {
  if (!game_probe_constants) return;
  native->CSSetConstantBuffers(PROBE_CONSTANTS_SLOT, 1, game_probe_constants.GetAddressOf());
  game_probe_constants.Reset();
}

// Before the probes' rays (cs DC860C60, once a frame). True: the game's dispatch runs.
static bool OnProbeRays(reshade::api::command_list* cmd_list) {
  if (!sky_only.load(std::memory_order_relaxed)) return true;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  if (native != nullptr) {
    SkyOnlyProbeConstants(native);
  }
  return true;
}

static void OnProbeRaysDone(reshade::api::command_list* cmd_list) {
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  if (native != nullptr) {
    GameProbeConstants(native);
  }
}

static void OnDestroyDevice(reshade::api::device*) {
  probe_constants.Reset();
  probe_constants_bytes = 0;
  no_power.Reset();
  game_probe_constants.Reset();
}

static void Install() {
  reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
  reshade::register_event<reshade::addon_event::init_resource>(OnInitResource);
  reshade::register_event<reshade::addon_event::destroy_resource>(OnDestroyResource);
  reshade::register_event<reshade::addon_event::map_buffer_region>(OnMapBufferRegion);
  reshade::register_event<reshade::addon_event::unmap_buffer_region>(OnUnmapBufferRegion);
  reshade::register_event<reshade::addon_event::update_buffer_region>(OnUpdateBufferRegion);
  reshade::register_event<reshade::addon_event::update_buffer_region_command>(OnUpdateBufferRegionCommand);
}

static void Uninstall() {
  off.store(false, std::memory_order_relaxed);
  sky_only.store(false, std::memory_order_relaxed);
  reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
  OnDestroyDevice(nullptr);
  reshade::unregister_event<reshade::addon_event::init_resource>(OnInitResource);
  reshade::unregister_event<reshade::addon_event::destroy_resource>(OnDestroyResource);
  reshade::unregister_event<reshade::addon_event::map_buffer_region>(OnMapBufferRegion);
  reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(OnUnmapBufferRegion);
  reshade::unregister_event<reshade::addon_event::update_buffer_region>(OnUpdateBufferRegion);
  reshade::unregister_event<reshade::addon_event::update_buffer_region_command>(OnUpdateBufferRegionCommand);
  buffer_writes::Clear(&lighting);
}

}  // namespace twwarhammer3::game_gi
