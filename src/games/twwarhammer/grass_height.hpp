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

// Grass Height Distance (Attila's row; Attila's own: twattila/grass_fade.hpp and its grass vertex shaders): Warhammer's
// grass shaders (rigidgrass(wet)) read their distance from the grass_PS constants, g_inv_lod_distance (c0.x; the RDEF's
// default 0.01: 100 m), not from a literal as Attila's. The main and shadow vertex shaders shrink each blade by
// saturate(distance x it), the G-buffer pixel shaders blend its normal into the ground's by it, the depth pre-pass drops
// blades past 1 / it. As the game creates them each read of it becomes rT.x after `mul rT.x, cbK[0].x, cb8[0].x` (rT a new
// temporary), and their draws (by the vertex shaders: every grass pixel shader draws with one of them) bind cb8 with
// 1 / Grass Height Distance: 1 gives the game's bits (test/tw_warhammer_shaders part 18). The rays' grass captures and
// Object Motion's grass divide the same constant by the row (rt_capture_grass_*, motion_grass_grass_*).
namespace twwarhammer::grass_height {

using Microsoft::WRL::ComPtr;

// The game's grass shaders and grass_PS's slot in each (fxc -dumpbin: every read of its c0.x listed in the comments).
struct GrassShader {
  uint32_t hash = 0;
  uint32_t slot = 0;
};
constexpr std::array<GrassShader, 8> SHADERS = {{
    {.hash = 0x1CF62070, .slot = 2},  // vs40_main_grass (sm_4_0): mul_sat r0.x, r0.x, cb2[0].x
    {.hash = 0x851C6D2A, .slot = 2},  // vs40_main_grass (sm_5_0), the same
    {.hash = 0x14A9C730, .slot = 2},  // vs40_shadow_grass, the same
    {.hash = 0x1513E0C0, .slot = 2},  // ps40_gbuffer_grass: mul_sat r0.y, r0.y, cb2[0].x
    {.hash = 0x9C41D092, .slot = 2},  // ps40_gbuffer_no_clip_grass, the same
    {.hash = 0xF2C1424B, .slot = 2},  // ps50_gbuffer_grass_ms: mul_sat r0.z, r0.y, cb2[0].x
    {.hash = 0x5A496734, .slot = 1},  // ps40_gbuffer_zprefill_grass: div r0.y, l(1), cb1[0].x
    {.hash = 0x58577E55, .slot = 1},  // ps50_gbuffer_zprefill_grass_ms: mul_sat r0.y, r0.y, cb1[0].x
}};
constexpr std::array<uint32_t, 3> VERTEX_SHADERS = {0x1CF62070, 0x851C6D2A, 0x14A9C730};
constexpr UINT SLOT = 8;  // free in every grass shader (they use cb0-2) and in the addon's other patches

namespace internal {

struct Runtime {
  std::mutex mutex;
  const float* row = nullptr;  // Grass Height Distance (Install)
  std::unordered_map<uint32_t, std::vector<uint8_t>> patched;  // by the game's hash (kept: it may create them again)
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11Buffer> buffer;
  float uploaded = -1.f;
  ComPtr<ID3D11Buffer> game_vertex_buffer;  // the draw's own at SLOT, put back after it
  ComPtr<ID3D11Buffer> game_pixel_buffer;
};

static Runtime runtime;

// `code` with each read of cb`slot`[0].x (one component, or .xxxx) through a new temporary x cb8[0].x; empty: no such
// read, or not a program this can read.
inline std::vector<uint8_t> Patch(std::span<const uint8_t> code, uint32_t slot) {
  constexpr uint32_t OP_MUL = 56;
  constexpr uint32_t OPERAND_TEMP = 0;
  constexpr uint32_t OPERAND_CONSTANT_BUFFER = 8;
  // rT.x as a destination (mask x), cb#[#].x (one component, 2D immediate index), dcl_constantbuffer cb8[1].
  constexpr uint32_t TEMP_X_DESTINATION = 2u | (0u << 2) | (1u << 4) | (OPERAND_TEMP << 12) | (1u << 20);
  constexpr uint32_t CONSTANT_X = 2u | (2u << 2) | (OPERAND_CONSTANT_BUFFER << 12) | (2u << 20);
  constexpr std::array<uint32_t, 4> DECLARATION = {0x04000059, 0x00208E46, SLOT, 1};
  twattila::dxbc::Program program = twattila::dxbc::Read(code);
  auto& tokens = program.tokens;
  if (tokens.empty()) return {};
  size_t temps_at = 0;
  const size_t first_instruction = twattila::dxbc::internal::FirstInstruction(tokens, &temps_at);
  if (first_instruction == 0 || temps_at == 0) return {};
  const uint32_t temp = tokens[temps_at];
  std::vector<uint32_t> out(tokens.begin(), tokens.begin() + static_cast<ptrdiff_t>(first_instruction));
  int reads = 0;
  for (size_t at = first_instruction; at < tokens.size();) {
    const size_t length = twattila::dxbc::Length(tokens, at);
    if (length == 0) return {};
    const size_t end = at + length;
    std::vector<uint32_t> instruction(tokens.begin() + static_cast<ptrdiff_t>(at), tokens.begin() + static_cast<ptrdiff_t>(end));
    bool reads_it = false;
    // Its operands (custom data has none): each read of grass_PS's c0.x becomes rT with the operand's own selection.
    if ((tokens[at] & 0x7FF) != twattila::dxbc::OP_CUSTOMDATA) {
      std::vector<uint32_t> rewritten(tokens.begin() + static_cast<ptrdiff_t>(at),
                                      tokens.begin() + static_cast<ptrdiff_t>(twattila::dxbc::internal::FirstOperand(tokens, at)));
      for (size_t next = rewritten.size() + at; next < end;) {
        twattila::dxbc::Operand operand;
        const size_t operand_length = twattila::dxbc::ParseOperand(tokens, next, end, &operand);
        if (operand_length == 0) return {};
        const bool x_only = (operand.mode == 2 && operand.select == 0) || (operand.mode == 1 && operand.select == 0);
        if (operand.type == OPERAND_CONSTANT_BUFFER && !operand.relative && operand.index[0] == slot && operand.index[1] == 0 && x_only) {
          // The same token with type temp and one immediate index, then its extended tokens (modifiers), then rT.
          const uint32_t token = tokens[next];
          rewritten.push_back((token & ~((0xFFu << 12) | (0x3u << 20) | (0x1FFu << 22))) | (OPERAND_TEMP << 12) | (1u << 20));
          size_t extended = next;
          while ((tokens[extended] >> 31) != 0) rewritten.push_back(tokens[++extended]);
          rewritten.push_back(temp);
          reads_it = true;
        } else {
          rewritten.insert(rewritten.end(), tokens.begin() + static_cast<ptrdiff_t>(next), tokens.begin() + static_cast<ptrdiff_t>(next + operand_length));
        }
        next += operand_length;
      }
      if (reads_it) {
        rewritten[0] = (rewritten[0] & ~(0x7Fu << 24)) | (static_cast<uint32_t>(rewritten.size()) << 24);
        instruction = std::move(rewritten);
      }
    }
    if (reads_it) {
      out.insert(out.end(), {OP_MUL | (9u << 24), TEMP_X_DESTINATION, temp, CONSTANT_X, slot, 0u, CONSTANT_X, SLOT, 0u});
      ++reads;
    }
    out.insert(out.end(), instruction.begin(), instruction.end());
    at = end;
  }
  if (reads == 0) return {};
  out[temps_at] = temp + 1;
  // Before dcl_temps (the declarations come first; dcl_temps is before every instruction).
  out.insert(out.begin() + static_cast<ptrdiff_t>(temps_at - 1), DECLARATION.begin(), DECLARATION.end());
  out[1] = static_cast<uint32_t>(out.size());
  tokens = std::move(out);
  return twattila::dxbc::Write(code, program);
}

inline bool OnCreatePipeline(reshade::api::device* device, reshade::api::pipeline_layout, uint32_t subobject_count,
                             const reshade::api::pipeline_subobject* subobjects) {
  if (device->get_api() != reshade::api::device_api::d3d11) return false;
  bool modified = false;
  for (uint32_t index = 0; index < subobject_count; ++index) {
    if (subobjects[index].type != reshade::api::pipeline_subobject_type::vertex_shader
        && subobjects[index].type != reshade::api::pipeline_subobject_type::pixel_shader) {
      continue;
    }
    auto* desc = static_cast<reshade::api::shader_desc*>(subobjects[index].data);
    if (desc == nullptr || desc->code == nullptr || desc->code_size == 0) continue;
    const std::span<const uint8_t> code(static_cast<const uint8_t*>(desc->code), desc->code_size);
    // The game's hash, also of a shader the render scale patched first.
    const uint32_t hash = twattila::view_share::OriginalHash(desc->code).value_or(renodx::utils::hash::ComputeCRC32(code.data(), code.size()));
    const auto shader = std::ranges::find(SHADERS, hash, &GrassShader::hash);
    if (shader == SHADERS.end()) continue;
    const std::unique_lock lock(runtime.mutex);
    auto [entry, added] = runtime.patched.try_emplace(hash);
    if (added) {
      entry->second = Patch(code, shader->slot);
      std::stringstream s;
      s << "[Total War] Grass Height Distance: grass shader " << PRINT_CRC32(hash)
        << (entry->second.empty() ? " has no distance to patch." : " patched.");
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

// A grass draw: 1 / Grass Height Distance at cb8 for its vertex and pixel shaders, the draw's own put back after it.
constexpr auto OnGrassDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
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
    if (!runtime.buffer || runtime.row == nullptr) return {};
    const float scale = 1.f / std::max(*runtime.row, 0.01f);
    if (runtime.uploaded != scale) {
      const std::array<float, 4> value = {scale, 0.f, 0.f, 0.f};
      native->UpdateSubresource(runtime.buffer.Get(), 0, nullptr, value.data(), 0, 0);
      runtime.uploaded = scale;
    }
    native->VSGetConstantBuffers(SLOT, 1, runtime.game_vertex_buffer.ReleaseAndGetAddressOf());
    native->PSGetConstantBuffers(SLOT, 1, runtime.game_pixel_buffer.ReleaseAndGetAddressOf());
    native->VSSetConstantBuffers(SLOT, 1, runtime.buffer.GetAddressOf());
    native->PSSetConstantBuffers(SLOT, 1, runtime.buffer.GetAddressOf());
    return {.post_callback = [](Context& post, const void*) {
      auto* post_native = reinterpret_cast<ID3D11DeviceContext*>(post.cmd_list->get_native());
      renodx::utils::directx::TryNativeFromReShadeProxy(&post_native);
      if (post_native == nullptr) return;
      const std::unique_lock post_lock(internal::runtime.mutex);
      post_native->VSSetConstantBuffers(SLOT, 1, internal::runtime.game_vertex_buffer.GetAddressOf());
      post_native->PSSetConstantBuffers(SLOT, 1, internal::runtime.game_pixel_buffer.GetAddressOf());
      internal::runtime.game_vertex_buffer.Reset();
      internal::runtime.game_pixel_buffer.Reset();
    }};
  }
};

// Before the game creates its shaders: its grass shaders read the distance through cb8, bound on their draws from `row`
// (Grass Height Distance: 1 the game's).
inline void Install(const float* row) {
  internal::runtime.row = row;
  renodx::utils::pipeline::RegisterOnCreateCallback(internal::OnCreatePipeline);
  for (const uint32_t hash : VERTEX_SHADERS) {
    renodx::utils::command_action::Register(
        OnGrassDraw, {.shader_hash = hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
}

inline void Uninstall() {
  renodx::utils::pipeline::UnregisterOnCreateCallback(internal::OnCreatePipeline);
  renodx::utils::command_action::Unregister(OnGrassDraw);
  const std::unique_lock lock(internal::runtime.mutex);
  internal::runtime.row = nullptr;
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

}  // namespace twwarhammer::grass_height
