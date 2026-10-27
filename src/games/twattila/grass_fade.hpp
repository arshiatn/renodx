/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <span>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <include/reshade.hpp>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/format.hpp"
#include "../../utils/hash.hpp"
#include "../../utils/pipeline.hpp"
#include "./dxbc.hpp"

// Grass Height Distance's colour fade (ATN debug9: grass stops in a circle ~100 m around the camera, whatever the
// grass sliders): the game's grass pixel shaders (rigidgrass(wet) ps30 gbuffer / gbuffer_no_clip / gbuffer_ms and
// forward_lit_oit / aoit, 13) blend each blade into the ground's colour by 100 m (mul rA.c, rB.c, l(0.01) before min:
// 1 - min(1, distance x 0.01)), so past it the blades are drawn but look like the ground. They are patched as the game
// creates them: that 0.01 read from cb12[0].x, which the addon binds on their draws (0.01 / Grass Height Distance, as
// the grass vertex shaders' height). cb12 unbound reads 0: no fade.
namespace twattila::grass_fade {

using Microsoft::WRL::ComPtr;

constexpr std::array<uint32_t, 13> HASHES = {
    0x7B0FF908, 0x39F985CD, 0x57591D38,                          // gbuffer_no_clip, gbuffer, gbuffer_ms
    0x9FA74069, 0x0F7F49E2, 0x1BDE4E9D, 0x5ADA3882, 0x88D39D8E,  // forward_lit_oit (each shadow method)
    0x50F6A618, 0x10FB512C, 0x88CBA801, 0x8CE99E1F, 0xF2C2DE70,  // forward_lit_aoit
};
constexpr UINT SLOT = 12;

namespace internal {

struct Runtime {
  std::mutex mutex;
  float fade = 0.01f;  // per metre: 0.01 / Grass Height Distance
  std::unordered_map<uint32_t, std::vector<uint8_t>> patched;  // by the game's hash (kept: it may create them again)
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11Buffer> buffer;
  float uploaded = -1.f;
};

static Runtime runtime;

// The fade's mul (its 0.01 the second source, a min next): that source from cb12[0].x, cb12 declared. Empty: not such
// a shader.
inline std::vector<uint8_t> Patch(std::span<const uint8_t> code) {
  constexpr uint32_t OP_MIN = 51, OP_MUL = 56, OP_DCL_TEMPS = 104;
  constexpr uint32_t IMMEDIATE_SCALAR = 0x00004001, HUNDREDTH = 0x3C23D70A;
  // cb#[#].x (select-1, 2D immediate index) and dcl_constantbuffer cb#[1], immediateIndexed.
  constexpr uint32_t CONSTANT_X = 2u | (2u << 2) | (8u << 12) | (2u << 20);
  constexpr std::array<uint32_t, 4> DECLARATION = {0x04000059, 0x00208E46, SLOT, 1};
  dxbc::Program program = dxbc::Read(code);
  auto& tokens = program.tokens;
  if (tokens.empty()) return {};
  size_t temps_at = 0;
  size_t fade_at = 0;
  for (size_t at = 2; at < tokens.size();) {
    const size_t length = dxbc::Length(tokens, at);
    if (length == 0) return {};
    const uint32_t opcode = tokens[at] & 0x7FF;
    if (opcode == OP_DCL_TEMPS) temps_at = at;
    if (opcode == OP_MUL && length == 7 && tokens[at + 5] == IMMEDIATE_SCALAR && tokens[at + 6] == HUNDREDTH
        && at + length < tokens.size() && (tokens[at + length] & 0x7FF) == OP_MIN) {
      if (fade_at != 0) return {};
      fade_at = at;
    }
    at += length;
  }
  if (temps_at == 0 || fade_at == 0) return {};
  tokens[fade_at] = (tokens[fade_at] & ~(0x7Fu << 24)) | (8u << 24);
  tokens[fade_at + 5] = CONSTANT_X;
  tokens[fade_at + 6] = SLOT;
  tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(fade_at + 7), 0u);
  // Before dcl_temps (the declarations come first; dcl_temps is before every instruction).
  tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(temps_at), DECLARATION.begin(), DECLARATION.end());
  tokens[1] = static_cast<uint32_t>(tokens.size());
  return dxbc::Write(code, program);
}

inline bool OnCreatePipeline(reshade::api::device* device, reshade::api::pipeline_layout, uint32_t subobject_count,
                             const reshade::api::pipeline_subobject* subobjects) {
  if (device->get_api() != reshade::api::device_api::d3d11) return false;
  bool modified = false;
  for (uint32_t index = 0; index < subobject_count; ++index) {
    if (subobjects[index].type != reshade::api::pipeline_subobject_type::pixel_shader) continue;
    auto* desc = static_cast<reshade::api::shader_desc*>(subobjects[index].data);
    if (desc == nullptr || desc->code == nullptr || desc->code_size == 0) continue;
    const std::span<const uint8_t> code(static_cast<const uint8_t*>(desc->code), desc->code_size);
    const uint32_t hash = renodx::utils::hash::ComputeCRC32(code.data(), code.size());
    if (std::ranges::find(HASHES, hash) == HASHES.end()) continue;
    const std::unique_lock lock(runtime.mutex);
    auto [entry, added] = runtime.patched.try_emplace(hash);
    if (added) {
      entry->second = Patch(code);
      std::stringstream s;
      s << "[Total War] Grass fade: grass shader " << PRINT_CRC32(hash)
        << (entry->second.empty() ? " has no fade to patch." : " patched.");
      reshade::log::message(reshade::log::level::info, s.str().c_str());
    }
    if (entry->second.empty()) continue;
    desc->code = entry->second.data();
    desc->code_size = entry->second.size();
    modified = true;
  }
  return modified;
}

}  // namespace internal

// A grass draw: the fade at cb12.
constexpr auto OnGrassDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  using Arguments = typename Context::ArgumentType;
  if constexpr (!std::is_same_v<Arguments, renodx::utils::command_action::DrawArguments>
                && !std::is_same_v<Arguments, renodx::utils::command_action::DrawIndexedArguments>) {
    return {};
  } else {
    if (command.cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return {};
    auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
    renodx::utils::directx::NativeFromReShadeProxy(&native);
    if (native == nullptr) return {};
    auto& runtime = internal::runtime;
    const std::unique_lock lock(runtime.mutex);
    ComPtr<ID3D11Device> device;
    native->GetDevice(device.GetAddressOf());
    if (runtime.device.Get() != device.Get()) {
      runtime.device = device;
      runtime.buffer.Reset();
      runtime.uploaded = -1.f;
      const D3D11_BUFFER_DESC desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
      device->CreateBuffer(&desc, nullptr, runtime.buffer.GetAddressOf());
    }
    if (!runtime.buffer) return {};
    if (runtime.uploaded != runtime.fade) {
      const std::array<float, 4> value = {runtime.fade, 0.f, 0.f, 0.f};
      native->UpdateSubresource(runtime.buffer.Get(), 0, nullptr, value.data(), 0, 0);
      runtime.uploaded = runtime.fade;
    }
    native->PSSetConstantBuffers(SLOT, 1, runtime.buffer.GetAddressOf());
    return {};
  }
};

// Every present: Grass Height Distance (1: the game's 100 m).
inline void Update(float height_distance) {
  const std::unique_lock lock(internal::runtime.mutex);
  internal::runtime.fade = 0.01f / std::max(height_distance, 0.01f);
}

// Before the game creates its shaders.
inline void Install() {
  renodx::utils::pipeline::RegisterOnCreateCallback(internal::OnCreatePipeline);
  for (const uint32_t hash : HASHES) {
    renodx::utils::command_action::Register(
        OnGrassDraw, {.shader_hash = hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
}

inline void Uninstall() {
  renodx::utils::pipeline::UnregisterOnCreateCallback(internal::OnCreatePipeline);
  renodx::utils::command_action::Unregister(OnGrassDraw);
}

// The game's device is going away: the buffer with it.
inline void OnDestroyDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  const std::unique_lock lock(internal::runtime.mutex);
  if (internal::runtime.device.Get() != native) return;
  internal::runtime.buffer.Reset();
  internal::runtime.device.Reset();
}

}  // namespace twattila::grass_fade
