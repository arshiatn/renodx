/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Grass Fade Distance. WH3's drawn grass (rigid_battle_grass: VS A4527F25, PS BD3CC4F1, D603D107) has no hard limit in its bytes: Attila's
// 100 m literal lives in fx_rigidgrass, drawn in no trace. Its fades start from the camera's distance: the VS squashes blades by
// grass_PS.g_inv_lod_distance and g_far_squash_distance, the PS moves its alpha threshold by the material's sp_distance_blend_*. While the
// row is above 1 the draws run copies with that distance x cb11[0].x (1 / row); at 1 the game's own shaders run.
#ifndef SRC_GAMES_TWWARHAMMER3_GRASS_DISTANCE_HPP_
#define SRC_GAMES_TWWARHAMMER3_GRASS_DISTANCE_HPP_

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>

#include <include/reshade.hpp>

#include "../../utils/command_action.hpp"
#include "../../utils/log.hpp"
#include "../../utils/pipeline.hpp"
#include "../twattila/dxbc.hpp"

namespace twwarhammer3::grass_distance {

using Microsoft::WRL::ComPtr;

constexpr uint32_t GRASS_VS = 0xA4527F25;
constexpr std::array<uint32_t, 3> GRASS_SHADERS = {GRASS_VS, 0xBD3CC4F1, 0xD603D107};
constexpr UINT DISTANCE_SLOT = 11;

inline float row = 1.f;

struct GrassShader {
  std::vector<uint8_t> code;
  ComPtr<ID3D11VertexShader> vertex;
  ComPtr<ID3D11PixelShader> pixel;
  bool refused = false;
};
inline std::unordered_map<uint32_t, GrassShader> shaders;
inline std::mutex shaders_mutex;
struct Objects {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11Buffer> constants;
  float written = 0.f;
  ComPtr<ID3D11VertexShader> game_vertex;
  ComPtr<ID3D11PixelShader> game_pixel;
};
inline Objects objects;

// The first sqrt in all three is the camera's distance (dp3 of position - cb0[0], read per shader): mul rX, rX, cb11[0].xxxx after it.
inline std::vector<uint8_t> PatchDistance(std::span<const uint8_t> code) {
  constexpr uint32_t OP_MUL = 56, OP_SQRT = 75, OP_DCL_CONSTANT_BUFFER = 89, OP_DCL_TEMPS = 104;
  twattila::dxbc::Program program = twattila::dxbc::Read(code);
  auto& tokens = program.tokens;
  if (tokens.empty()) return {};
  size_t temps_at = 0;
  size_t sqrt_at = 0;
  for (size_t at = 2; at < tokens.size() && sqrt_at == 0;) {
    const size_t length = twattila::dxbc::Length(tokens, at);
    if (length == 0) return {};
    const uint32_t opcode = tokens[at] & 0x7FF;
    if (opcode == OP_DCL_TEMPS) temps_at = at;
    if (opcode == OP_DCL_CONSTANT_BUFFER && tokens[at + 2] == DISTANCE_SLOT) return {};
    if (opcode == OP_SQRT) sqrt_at = at;
    at += length;
  }
  if (temps_at == 0 || sqrt_at == 0 || (tokens[sqrt_at + 1] & 0x001FF003) != 0x00100002) return {};
  const uint32_t destination = tokens[sqrt_at + 1];
  const uint32_t temp = tokens[sqrt_at + 2];
  const std::array<uint32_t, 8> scale = {OP_MUL | (8u << 24), destination, temp, 0x00100E46, temp, 0x00208006, DISTANCE_SLOT, 0u};
  tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(sqrt_at + twattila::dxbc::Length(tokens, sqrt_at)), scale.begin(), scale.end());
  const std::array<uint32_t, 4> declaration = {OP_DCL_CONSTANT_BUFFER | (4u << 24), 0x00208E46, DISTANCE_SLOT, 1u};
  tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(temps_at), declaration.begin(), declaration.end());
  tokens[1] = static_cast<uint32_t>(tokens.size());
  return twattila::dxbc::Write(code, program);
}

inline void OnInitPipeline(renodx::utils::pipeline::PipelineInfo* info, uint32_t subobject_count, const reshade::api::pipeline_subobject* subobjects) {
  for (const auto& identity : info->shader_details) {
    if (std::ranges::find(GRASS_SHADERS, identity.shader_hash) == GRASS_SHADERS.end()) continue;
    for (uint32_t index = 0; index < subobject_count; ++index) {
      const auto type = subobjects[index].type;
      if (type != reshade::api::pipeline_subobject_type::vertex_shader && type != reshade::api::pipeline_subobject_type::pixel_shader) continue;
      const auto* desc = static_cast<const reshade::api::shader_desc*>(subobjects[index].data);
      if (desc == nullptr || desc->code == nullptr) continue;
      const auto* bytes = static_cast<const uint8_t*>(desc->code);
      const std::unique_lock lock(shaders_mutex);
      shaders[identity.shader_hash] = {.code = std::vector<uint8_t>(bytes, bytes + desc->code_size)};
    }
  }
}

constexpr auto OnGrassDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (row <= 1.f) return {};
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  const bool is_vertex = command.matched_shader_hash == GRASS_VS;
  ComPtr<ID3D11VertexShader> vertex;
  ComPtr<ID3D11PixelShader> pixel;
  {
    const std::unique_lock lock(shaders_mutex);
    if (objects.device != device) {
      const D3D11_BUFFER_DESC constants_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
      objects = {.device = device};
      device->CreateBuffer(&constants_desc, nullptr, objects.constants.GetAddressOf());
      for (auto& [hash, entry] : shaders) entry = {.code = std::move(entry.code)};
    }
    const auto entry = shaders.find(command.matched_shader_hash);
    if (entry == shaders.end()) return {};
    auto& grass = entry->second;
    if (!grass.vertex && !grass.pixel && !grass.refused) {
      const std::vector<uint8_t> code = PatchDistance(grass.code);
      grass.refused = code.empty()
                      || FAILED(is_vertex ? device->CreateVertexShader(code.data(), code.size(), nullptr, grass.vertex.GetAddressOf())
                                          : device->CreatePixelShader(code.data(), code.size(), nullptr, grass.pixel.GetAddressOf()));
      renodx::utils::log::i(std::format("[Warhammer III] Grass Fade Distance: shader {:08X} {}", command.matched_shader_hash,
                                        grass.refused ? "not as expected, stays the game's." : "patched."));
    }
    vertex = grass.vertex;
    pixel = grass.pixel;
  }
  if ((!vertex && !pixel) || !objects.constants) return {};
  if (objects.written != row) {
    objects.written = row;
    const std::array<float, 4> constants = {1.f / row, 0.f, 0.f, 0.f};
    native->UpdateSubresource(objects.constants.Get(), 0, nullptr, constants.data(), 0, 0);
  }
  if (is_vertex) {
    native->VSGetShader(objects.game_vertex.GetAddressOf(), nullptr, nullptr);
    native->VSSetShader(vertex.Get(), nullptr, 0);
    native->VSSetConstantBuffers(DISTANCE_SLOT, 1, objects.constants.GetAddressOf());
    return {.finalize_callback = [](Context& post, const void*) {
      auto* post_native = reinterpret_cast<ID3D11DeviceContext*>(post.cmd_list->get_native());
      ID3D11Buffer* no_constants = nullptr;
      post_native->VSSetShader(objects.game_vertex.Get(), nullptr, 0);
      post_native->VSSetConstantBuffers(DISTANCE_SLOT, 1, &no_constants);
      objects.game_vertex.Reset();
    }};
  }
  native->PSGetShader(objects.game_pixel.GetAddressOf(), nullptr, nullptr);
  native->PSSetShader(pixel.Get(), nullptr, 0);
  native->PSSetConstantBuffers(DISTANCE_SLOT, 1, objects.constants.GetAddressOf());
  return {.finalize_callback = [](Context& post, const void*) {
    auto* post_native = reinterpret_cast<ID3D11DeviceContext*>(post.cmd_list->get_native());
    ID3D11Buffer* no_constants = nullptr;
    post_native->PSSetShader(objects.game_pixel.Get(), nullptr, 0);
    post_native->PSSetConstantBuffers(DISTANCE_SLOT, 1, &no_constants);
    objects.game_pixel.Reset();
  }};
};

inline void Install() {
  renodx::utils::pipeline::RegisterOnInitCallback(OnInitPipeline);
  for (const uint32_t& hash : GRASS_SHADERS) {
    renodx::utils::command_action::Register(
        OnGrassDraw, {.shader_hash = hash,
                      .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW | renodx::utils::command_action::COMMAND_TYPE_INDIRECT});
  }
}

inline void Uninstall() {
  renodx::utils::command_action::Unregister(OnGrassDraw);
  renodx::utils::pipeline::UnregisterOnInitCallback(OnInitPipeline);
}

}  // namespace twwarhammer3::grass_distance

#endif  // SRC_GAMES_TWWARHAMMER3_GRASS_DISTANCE_HPP_
