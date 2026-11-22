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
#include "../twattila/dxbc.hpp"
#include "../twattila/view_share.hpp"

// VFX Brightness (Attila's vfx_base_brightness, Pharaoh's VfxBaseBrightness hook): the lit particles' colour x the slider,
// after their lighting and before their fog (Pharaoh's place in its twin 3F1B86E1). Every lit particle_vfx_main pixel
// shader of Troy's (ps_main_high_quality with and without the game's MSAA, each shadow method and shader model; effects
// quality 0-2's ps_main) holds that colour in the temporary a movc rC.xyz writes right before it tests g_draw_flags' fog
// bit (and rA.x, rB.w, cb_vfx[0].x; and rA.x, rA.x, l(0x40000000)): tools\tw\troy\par_t_vfx_anchors.py, one test each. They
// are patched as the game creates them: before that test mad rC.xyz, rC.xyzx, cb7[0].xxxx, rC.xyzx, cb7 declared; the addon
// binds cb7 on every one of their draws with the slider minus 1 (WH3's fog_strength form: whatever another draw left at cb7
// never reaches them). At 1 (or cb7 unbound: 0) the game's colour, to the bit (x 0 + x). ps_overlay (E76256B8, 8577C86B)
// stays out (not Pharaoh's either).
// test\tw_troy_shaders part 3: each patched program is the game's plus those two, and on WARP the game's bits at 1.
namespace twtroy::vfx_brightness {

using Microsoft::WRL::ComPtr;

constexpr std::array<uint32_t, 10> HASHES = {
    0x563BA3E8, 0xDE9336A0, 0x85909B70, 0x840735B1,  // ps_main_high_quality: shadows, baked only, none, shader model 4.1
    0x183804B3, 0x84267C32, 0xAC3F799C, 0x21FA05C9,  // ps_main_high_quality_ms (the game's MSAA), the same four
    0x9C8987DD, 0x0699391F,                          // ps_main: shader model 5, 4.1
};
// Free in all of them (they read cb0-cb5); 12 is the render scale's, 13 the add-on's.
// Not b8: Particle Resolution (twattila/particle_layer.hpp) binds its layer constants there on the particle draws.
constexpr UINT SLOT = 7;

// `code` with the brightness before the fog test. Empty: no such test, or no movc rC.xyz right before it.
inline std::vector<uint8_t> Patch(std::span<const uint8_t> code) {
  constexpr uint32_t OP_AND = 1, OP_ELSE = 18, OP_ENDIF = 21, OP_MAD = 50, OP_MOVC = 55, OP_DCL_TEMPS = 104;
  constexpr uint32_t IMMEDIATE_SCALAR = 0x00004001, FOG_BIT = 0x40000000;
  // rC.xyz (mask), rC.xyzx (swizzle), cb7[0].xxxx (swizzle, 2D immediate index); dcl_constantbuffer cb7[1], immediateIndexed.
  constexpr uint32_t TEMP_XYZ = 2u | (0x7u << 4) | (1u << 20);
  constexpr uint32_t TEMP_SWIZZLE_XYZX = 2u | (1u << 2) | (0x24u << 4) | (1u << 20);
  constexpr uint32_t CONSTANT_XXXX = 2u | (1u << 2) | (8u << 12) | (2u << 20);
  constexpr std::array<uint32_t, 4> DECLARATION = {0x04000059, 0x00208E46, SLOT, 1};
  twattila::dxbc::Program program = twattila::dxbc::Read(code);
  auto& tokens = program.tokens;
  if (tokens.empty()) return {};
  size_t temps_at = 0;
  size_t test_at = 0;
  size_t colour_at = 0;  // the movc's
  size_t last_movc = 0;  // the last movc rC.xyz, endif / else between it and here
  size_t previous = 0;
  for (size_t at = 2; at < tokens.size();) {
    const size_t length = twattila::dxbc::Length(tokens, at);
    if (length == 0) return {};
    const uint32_t opcode = tokens[at] & 0x7FF;
    if (opcode == OP_DCL_TEMPS) temps_at = at;
    // The second and: and rA.x, rA.x, l(0x40000000), after the first and (its cb_vfx read) and the colour's movc.
    if (opcode == OP_AND && length == 7 && tokens[at + 5] == IMMEDIATE_SCALAR && tokens[at + 6] == FOG_BIT && previous != 0
        && (tokens[previous] & 0x7FF) == OP_AND) {
      if (test_at != 0) return {};
      test_at = previous;
      colour_at = last_movc;
    }
    if (opcode == OP_MOVC && tokens[at + 1] == TEMP_XYZ) {
      last_movc = at;
    } else if (opcode != OP_ENDIF && opcode != OP_ELSE && opcode != OP_AND) {
      last_movc = 0;
    }
    previous = at;
    at += length;
  }
  if (temps_at == 0 || test_at == 0 || colour_at == 0) return {};
  const uint32_t colour = tokens[colour_at + 2];
  const std::array<uint32_t, 10> brightness = {OP_MAD | (10u << 24), TEMP_XYZ, colour, TEMP_SWIZZLE_XYZX, colour, CONSTANT_XXXX, SLOT, 0,
                                               TEMP_SWIZZLE_XYZX, colour};
  tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(test_at), brightness.begin(), brightness.end());
  // Before dcl_temps (the declarations come first; dcl_temps is before every instruction).
  tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(temps_at), DECLARATION.begin(), DECLARATION.end());
  tokens[1] = static_cast<uint32_t>(tokens.size());
  return twattila::dxbc::Write(code, program);
}

namespace internal {

struct Runtime {
  std::mutex mutex;
  float brightness = 1.f;
  std::unordered_map<uint32_t, std::vector<uint8_t>> patched;  // by the game's hash (kept: it may create them again)
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11Buffer> buffer;
  float uploaded = -1.f;
  ComPtr<ID3D11Buffer> game_buffer;  // the draw's own at SLOT, put back after it
};

static Runtime runtime;

inline bool OnCreatePipeline(reshade::api::device* device, reshade::api::pipeline_layout, uint32_t subobject_count,
                             const reshade::api::pipeline_subobject* subobjects) {
  if (device->get_api() != reshade::api::device_api::d3d11) return false;
  bool modified = false;
  for (uint32_t index = 0; index < subobject_count; ++index) {
    if (subobjects[index].type != reshade::api::pipeline_subobject_type::pixel_shader) continue;
    auto* desc = static_cast<reshade::api::shader_desc*>(subobjects[index].data);
    if (desc == nullptr || desc->code == nullptr || desc->code_size == 0) continue;
    const std::span<const uint8_t> code(static_cast<const uint8_t*>(desc->code), desc->code_size);
    // The game's hash, also of a shader the render scale patched first (the soft particles' depth read).
    const uint32_t hash = twattila::view_share::OriginalHash(desc->code).value_or(renodx::utils::hash::ComputeCRC32(code.data(), code.size()));
    if (std::ranges::find(HASHES, hash) == HASHES.end()) continue;
    const std::unique_lock lock(runtime.mutex);
    auto [entry, added] = runtime.patched.try_emplace(hash);
    if (added) {
      entry->second = Patch(code);
      std::stringstream s;
      s << "[Total War] VFX Brightness: particle shader " << PRINT_CRC32(hash)
        << (entry->second.empty() ? " has no fog test to patch before." : " patched.");
      reshade::log::message(reshade::log::level::info, s.str().c_str());
    }
    if (entry->second.empty()) continue;
    twattila::view_share::Repatched(desc->code, entry->second.data(), hash);
    desc->code = entry->second.data();
    desc->code_size = entry->second.size();
    modified = true;
  }
  return modified;
}

}  // namespace internal

// A particle draw: the slider minus 1 at cb7 for it, the draw's own put back after it (in finalize: after Particle
// Resolution's, which draws its Full-Resolution Particle Edges again there).
constexpr auto OnParticleDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  using Arguments = typename Context::ArgumentType;
  if constexpr (!std::is_same_v<Arguments, renodx::utils::command_action::DrawArguments>
                && !std::is_same_v<Arguments, renodx::utils::command_action::DrawIndexedArguments>) {
    return {};
  } else {
    if (command.cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return {};
    auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&native);
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
    // Without our buffer cb7 is bound null, never left at another draw's: the patched shaders read 0, the game's colour.
    if (runtime.buffer && runtime.uploaded != runtime.brightness) {
      const std::array<float, 4> value = {runtime.brightness - 1.f, 0.f, 0.f, 0.f};
      native->UpdateSubresource(runtime.buffer.Get(), 0, nullptr, value.data(), 0, 0);
      runtime.uploaded = runtime.brightness;
    }
    native->PSGetConstantBuffers(SLOT, 1, runtime.game_buffer.ReleaseAndGetAddressOf());
    native->PSSetConstantBuffers(SLOT, 1, runtime.buffer.GetAddressOf());
    return {.finalize_callback = [](Context& post, const void*) {
      auto* post_native = reinterpret_cast<ID3D11DeviceContext*>(post.cmd_list->get_native());
      renodx::utils::directx::TryNativeFromReShadeProxy(&post_native);
      if (post_native == nullptr) return;
      const std::unique_lock post_lock(internal::runtime.mutex);
      post_native->PSSetConstantBuffers(SLOT, 1, internal::runtime.game_buffer.GetAddressOf());
      internal::runtime.game_buffer.Reset();
    }};
  }
};

// Every present: the slider (1: the game's).
inline void Update(float brightness) {
  const std::unique_lock lock(internal::runtime.mutex);
  internal::runtime.brightness = brightness;
}

// Before the game creates its shaders.
inline void Install() {
  renodx::utils::pipeline::RegisterOnCreateCallback(internal::OnCreatePipeline);
  for (const uint32_t hash : HASHES) {
    renodx::utils::command_action::Register(
        OnParticleDraw, {.shader_hash = hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
}

inline void Uninstall() {
  renodx::utils::pipeline::UnregisterOnCreateCallback(internal::OnCreatePipeline);
  renodx::utils::command_action::Unregister(OnParticleDraw);
}

// The game's device is going away: the buffer with it.
inline void OnDestroyDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  const std::unique_lock lock(internal::runtime.mutex);
  if (internal::runtime.device.Get() != native) return;
  internal::runtime.buffer.Reset();
  internal::runtime.device.Reset();
}

}  // namespace twtroy::vfx_brightness
