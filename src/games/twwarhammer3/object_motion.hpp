/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Object motion the game leaves out. Every G-buffer draw can write its own motion into rt3 (R16G16_FLOAT) and set bit 4 of the flag byte
// in rt2.a, and then the fullscreen fill (ps D409F9B5) keeps it; a draw that doesn't gets the camera's motion only. These families move
// but take that path: capture flags, wind planes, tentacles, baked animation (their vertex shaders read time_in_sec and write a zero
// previous position), glowing, dissolving and iridescent weapons on moving rows (their vertex shader has the rows, their pixel shader
// doesn't use them). The draw itself writes it: the game's pixel shader with the game's own velocity lines added
// (dxbc.hpp AddVelocityTarget), and where needed the game's vertex shader with its position again at last frame's time
// (dxbc.hpp AddPreviousPosition; every other output keeps the game's bits).
//
// Not here: unit banners by wind, liquid and scrolling props (3689A926, 033108E7, 22B5CCC7, BC3F3209, 42E93502, 0EB3AF84, 006F8ED8,
// A46C0E71: bones or rows in o1, sway after them, so no copy of the program gives their last position), drawn in no trace; particles.
#ifndef SRC_GAMES_TWWARHAMMER3_OBJECT_MOTION_HPP_
#define SRC_GAMES_TWWARHAMMER3_OBJECT_MOTION_HPP_

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <format>
#include <shared_mutex>
#include <span>
#include <unordered_map>
#include <vector>

#include <include/reshade.hpp>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "../../utils/pipeline.hpp"
#include "../twattila/dxbc.hpp"

namespace twwarhammer3::object_motion {

// Lists: tools b5_lists.py, from the dump's bytes and shader families: read time_in_sec, not prev_time_in_sec, o1.xyz = 0, one ret.
// Vertex shaders that move by time, previous position zero.
constexpr std::array<uint32_t, 51> TIME_VS = {
    0x01B68DA4, 0x081589C0, 0x0A953B85, 0x108C5B42, 0x11B89E41, 0x1C2B44CB, 0x1C3500F0, 0x219075F3, 0x233A0281,
    0x287289CA, 0x28E3B705, 0x2F5EF5F6, 0x31FBADEA, 0x32BABBF9, 0x345D2032, 0x3544E0AB, 0x4245BDA2, 0x444F1190,
    0x4686D715, 0x48919A45, 0x49B47E14, 0x4C3AE8E5, 0x5454018D, 0x5BD30610, 0x606279B7, 0x7128C228, 0x715EB9B6,
    0x72CDA7A1, 0x7D09C93F, 0x912E2CC2, 0x931CB000, 0xAA3A246F, 0xB374DBE3, 0xB3C3528D, 0xB436F5A9, 0xBE5B1745,
    0xC0708722, 0xC0F92487, 0xC6627B3B, 0xC880AAFD, 0xD29F3ED5, 0xD6F8031E, 0xD7D6FF73, 0xE2D5FAE0, 0xE32F524D,
    0xE3C30D87, 0xE8144D38, 0xE97BFE5D, 0xEF76ABCC, 0xF8A50513, 0xFBD91940,
};

// Vertex shaders whose rows give a real previous position, in families with the game's own velocity PS.
constexpr std::array<uint32_t, 2> MOVING_VS = {
    0x2C45437C, 0x8EE34EC5,
};

// Pixel shaders of those families with no velocity, the flag byte from literals, `camera` at b0.
constexpr std::array<uint32_t, 83> GAP_PS = {
    0x0303BB6B, 0x039AE4C1, 0x042CCA6F, 0x062AC0DC, 0x068D34EB, 0x07936934, 0x08DA2D7B, 0x09B6A2F6, 0x0A3B8E6F,
    0x0BE7D0E2, 0x11E89FA7, 0x11FDCFFB, 0x19BE7FB4, 0x1B314D66, 0x1BA772E0, 0x1CA7B40C, 0x1D169293, 0x1D8EDC36,
    0x206D59E7, 0x38337236, 0x3AC36B3B, 0x3B83A698, 0x3DE8DB53, 0x45EAD336, 0x46D18DFA, 0x478CEF63, 0x4DED19ED,
    0x5018CEC5, 0x53007DD7, 0x571D1C82, 0x5B231357, 0x5BB1738B, 0x5CB4B1C2, 0x5CEE8A04, 0x6598516C, 0x68289A85,
    0x6A5EEE23, 0x6F976263, 0x726296FB, 0x7C0470C0, 0x80ECFFB5, 0x83716A64, 0x91342F9F, 0x92263B2B, 0x9408D7B4,
    0x989BED69, 0x99E7D053, 0x9D12EC29, 0xA134F111, 0xA5DC848E, 0xA646C789, 0xA7CE2AFD, 0xAA193E6B, 0xAA7B03E2,
    0xAC08B6C4, 0xB0553821, 0xB93E5F40, 0xBA8D1BBC, 0xC314AC65, 0xC8DCA87A, 0xC998A3A2, 0xCC585855, 0xCD57DDEA,
    0xD0E465B6, 0xD1C33293, 0xD280A8EE, 0xD9BECACC, 0xDD7C5040, 0xDEABDB08, 0xDEBB617F, 0xDEF3510A, 0xDEFAC68C,
    0xE0A7FA3C, 0xE453EF19, 0xE5FFBEC1, 0xE7A95378, 0xE9C887F1, 0xEC600E7E, 0xF06F5009, 0xF17F3DF1, 0xF1ED86DF,
    0xF6C71666, 0xFBAE4D18,
};

constexpr uint32_t MOTION_FILL_PS = 0xD409F9B5;

using Microsoft::WRL::ComPtr;

// Our patched shader of a game's, made at its first draw. A moving VS has no code: it stays the game's.
struct Patched {
  std::vector<uint8_t> code;
  ComPtr<ID3D11DeviceChild> shader;
  bool refused = false;
};

inline std::shared_mutex mutex;
inline std::unordered_map<uint64_t, Patched> vertex_shaders;
inline std::unordered_map<uint64_t, Patched> pixel_shaders;
inline bool (*active)() = nullptr;
inline bool (*water_active)() = nullptr;
inline std::atomic_bool fill_logged = false;
inline std::atomic_bool fill_refused = false;

struct Swapped {
  ComPtr<ID3D11VertexShader> vertex;
  ComPtr<ID3D11PixelShader> pixel;
  ComPtr<ID3D11RenderTargetView> fill_target;
  ComPtr<ID3D11DepthStencilView> fill_depth;
  ComPtr<ID3D11ShaderResourceView> fill_copy;
};
inline thread_local Swapped swapped;
inline ComPtr<ID3D11Resource> depth_texture;
inline ComPtr<ID3D11ShaderResourceView> depth_view;

inline void OnInitPipeline(renodx::utils::pipeline::PipelineInfo* info, uint32_t subobject_count, const reshade::api::pipeline_subobject* subobjects) {
  for (const auto& identity : info->shader_details) {
    const auto listed = [&](const auto& list) { return std::ranges::find(list, identity.shader_hash) != list.end(); };
    const bool time = listed(TIME_VS);
    const bool moving = listed(MOVING_VS);
    const bool gap = listed(GAP_PS);
    if (!time && !moving && !gap) continue;
    Patched entry;
    if (time || gap) {
      for (uint32_t subobject = 0; subobject < subobject_count; ++subobject) {
        const auto type = subobjects[subobject].type;
        if (type != reshade::api::pipeline_subobject_type::vertex_shader && type != reshade::api::pipeline_subobject_type::pixel_shader) continue;
        const auto* desc = static_cast<const reshade::api::shader_desc*>(subobjects[subobject].data);
        if (desc == nullptr || desc->code == nullptr) continue;
        const auto* bytes = static_cast<const uint8_t*>(desc->code);
        entry.code.assign(bytes, bytes + desc->code_size);
      }
      if (entry.code.empty()) continue;
    }
    const std::unique_lock lock(mutex);
    auto& shaders = (gap ? pixel_shaders : vertex_shaders);
    shaders.insert_or_assign(info->pipeline.handle, std::move(entry));
  }
}

inline void OnDestroyPipeline(const renodx::utils::pipeline::PipelineInfo& info) {
  const std::unique_lock lock(mutex);
  vertex_shaders.erase(info.pipeline.handle);
  pixel_shaders.erase(info.pipeline.handle);
}

// Keys only: the swap happens where rt3 is the velocity (water's pass has colour there, and these shaders may run in other passes).
inline bool VelocityBound(ID3D11DeviceContext* native) {
  std::array<ID3D11RenderTargetView*, 4> targets = {};
  native->OMGetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), nullptr);
  bool velocity = false;
  if (targets[3] != nullptr) {
    D3D11_RENDER_TARGET_VIEW_DESC desc = {};
    targets[3]->GetDesc(&desc);
    velocity = desc.Format == DXGI_FORMAT_R16G16_FLOAT;
  }
  for (auto* target : targets) {
    if (target != nullptr) target->Release();
  }
  return velocity;
}

constexpr auto OnPixelDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (!active()) return {};
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  if (native == nullptr || !VelocityBound(native)) return {};
  ComPtr<ID3D11VertexShader> game_vertex;
  native->VSGetShader(game_vertex.GetAddressOf(), nullptr, nullptr);
  ComPtr<ID3D11PixelShader> game_pixel;
  native->PSGetShader(game_pixel.GetAddressOf(), nullptr, nullptr);
  const auto vertex_key = reinterpret_cast<uint64_t>(game_vertex.Get());
  const auto pixel_key = reinterpret_cast<uint64_t>(game_pixel.Get());
  ComPtr<ID3D11DeviceChild> vertex_child;
  ComPtr<ID3D11DeviceChild> pixel_child;
  bool needs_vertex = false;
  bool ready = false;
  {
    const std::shared_lock lock(mutex);
    const auto vertex = vertex_shaders.find(vertex_key);
    const auto pixel = pixel_shaders.find(pixel_key);
    // A static vertex shader's previous position is 0: no motion to give.
    if (vertex == vertex_shaders.end() || pixel == pixel_shaders.end()) return {};
    if (vertex->second.refused || pixel->second.refused) return {};
    needs_vertex = !vertex->second.code.empty();
    vertex_child = vertex->second.shader;
    pixel_child = pixel->second.shader;
    ready = pixel_child && (!needs_vertex || vertex_child);
  }
  if (!ready) {
    const std::unique_lock lock(mutex);
    const auto vertex = vertex_shaders.find(vertex_key);
    const auto pixel = pixel_shaders.find(pixel_key);
    if (vertex == vertex_shaders.end() || pixel == pixel_shaders.end()) return {};
    ComPtr<ID3D11Device> device;
    native->GetDevice(device.GetAddressOf());
    if (!pixel->second.shader && !pixel->second.refused) {
      const std::vector<uint8_t> code = twattila::dxbc::AddVelocityTarget(pixel->second.code);
      ComPtr<ID3D11PixelShader> created;
      if (code.empty() || FAILED(device->CreatePixelShader(code.data(), code.size(), nullptr, created.GetAddressOf()))) {
        pixel->second.refused = true;
        renodx::utils::log::w("[Warhammer III] Object motion: a pixel shader wasn't patched (it stays the game's).");
      } else {
        pixel->second.shader = created;
        renodx::utils::log::i(std::format("[Warhammer III] Object motion: ps {:08X} writes its motion.", command.matched_shader_hash));
      }
    }
    if (needs_vertex && !vertex->second.shader && !vertex->second.refused) {
      const std::vector<uint8_t> code = twattila::dxbc::AddPreviousPosition(vertex->second.code);
      ComPtr<ID3D11VertexShader> created;
      if (code.empty() || FAILED(device->CreateVertexShader(code.data(), code.size(), nullptr, created.GetAddressOf()))) {
        vertex->second.refused = true;
        renodx::utils::log::w("[Warhammer III] Object motion: a vertex shader wasn't patched (its draws stay the game's).");
      } else {
        vertex->second.shader = created;
        renodx::utils::log::i(std::format("[Warhammer III] Object motion: a vertex shader's previous position, with ps {:08X}.", command.matched_shader_hash));
      }
    }
    if (vertex->second.refused || pixel->second.refused) return {};
    vertex_child = vertex->second.shader;
    pixel_child = pixel->second.shader;
  }
  ComPtr<ID3D11PixelShader> our_pixel;
  pixel_child.As(&our_pixel);
  if (!our_pixel) return {};
  swapped.pixel = game_pixel;
  native->PSSetShader(our_pixel.Get(), nullptr, 0);
  if (needs_vertex) {
    ComPtr<ID3D11VertexShader> our_vertex;
    vertex_child.As(&our_vertex);
    if (our_vertex) {
      swapped.vertex = game_vertex;
      native->VSSetShader(our_vertex.Get(), nullptr, 0);
    }
  }
  return {.post_callback = [](Context& post, const void*) {
    auto* post_native = reinterpret_cast<ID3D11DeviceContext*>(post.cmd_list->get_native());
    renodx::utils::directx::NativeFromReShadeProxy(&post_native);
    if (post_native == nullptr) return;
    post_native->PSSetShader(swapped.pixel.Get(), nullptr, 0);
    swapped.pixel.Reset();
    if (swapped.vertex) {
      post_native->VSSetShader(swapped.vertex.Get(), nullptr, 0);
      swapped.vertex.Reset();
    }
  }};
};

// The fill's camera motion at the depth with water in it: t1 is a copy made before water (traces: water writes the bound depth), so the
// water's pixels moved as the riverbed under them. The fill tests no depth or stencil, so the depth can leave the target for its draw.
// The copy itself stays as it is: reflections read it.
constexpr auto OnMotionFill = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (!water_active() || fill_refused.load(std::memory_order_relaxed)) return {};
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  if (native == nullptr) return {};
  ComPtr<ID3D11RenderTargetView> target;
  ComPtr<ID3D11DepthStencilView> depth;
  native->OMGetRenderTargets(1, target.GetAddressOf(), depth.GetAddressOf());
  if (!target || !depth) return {};
  ComPtr<ID3D11Resource> resource;
  depth->GetResource(resource.GetAddressOf());
  if (resource != depth_texture) {
    ComPtr<ID3D11Device> device;
    native->GetDevice(device.GetAddressOf());
    const D3D11_SHADER_RESOURCE_VIEW_DESC desc = {
        .Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS,
        .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
        .Texture2D = {.MostDetailedMip = 0, .MipLevels = 1},
    };
    depth_view.Reset();
    if (FAILED(device->CreateShaderResourceView(resource.Get(), &desc, depth_view.GetAddressOf()))) {
      fill_refused = true;
      renodx::utils::log::w("[Warhammer III] Object motion: the depth has no shader view: water's motion stays the game's.");
      return {};
    }
    depth_texture = resource;
  }
  native->PSGetShaderResources(1, 1, swapped.fill_copy.ReleaseAndGetAddressOf());
  swapped.fill_target = target;
  swapped.fill_depth = depth;
  native->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
  native->PSSetShaderResources(1, 1, depth_view.GetAddressOf());
  if (!fill_logged.exchange(true, std::memory_order_relaxed)) {
    renodx::utils::log::i("[Warhammer III] First run: motion fill at the depth with water.");
  }
  return {.post_callback = [](Context& post, const void*) {
    auto* post_native = reinterpret_cast<ID3D11DeviceContext*>(post.cmd_list->get_native());
    renodx::utils::directx::NativeFromReShadeProxy(&post_native);
    if (post_native == nullptr) return;
    ID3D11ShaderResourceView* no_view = nullptr;
    post_native->PSSetShaderResources(1, 1, &no_view);
    post_native->OMSetRenderTargets(1, swapped.fill_target.GetAddressOf(), swapped.fill_depth.Get());
    post_native->PSSetShaderResources(1, 1, swapped.fill_copy.GetAddressOf());
    swapped.fill_target.Reset();
    swapped.fill_depth.Reset();
    swapped.fill_copy.Reset();
  }};
};

inline void Install(bool (*is_active)(), bool (*is_water_active)()) {
  active = is_active;
  water_active = is_water_active;
  renodx::utils::pipeline::RegisterOnInitCallback(OnInitPipeline);
  renodx::utils::pipeline::RegisterOnDestroyCallback(OnDestroyPipeline);
  for (const uint32_t& hash : GAP_PS) {
    renodx::utils::command_action::Register(OnPixelDraw, {.shader_hash = hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
  renodx::utils::command_action::Register(OnMotionFill, {.shader_hash = MOTION_FILL_PS, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
}

// Addon detach: the callbacks live in storage shared by the addons, which outlives this module.
inline void Uninstall() {
  renodx::utils::pipeline::UnregisterOnInitCallback(OnInitPipeline);
  renodx::utils::pipeline::UnregisterOnDestroyCallback(OnDestroyPipeline);
  renodx::utils::command_action::Unregister(OnPixelDraw);
  renodx::utils::command_action::Unregister(OnMotionFill);
}

inline void OnDestroyDevice(reshade::api::device*) {
  const std::unique_lock lock(mutex);
  for (auto* shaders : {&vertex_shaders, &pixel_shaders}) {
    for (auto& [handle, entry] : *shaders) entry.shader.Reset();
  }
  depth_view.Reset();
  depth_texture.Reset();
  swapped = {};
}

}  // namespace twwarhammer3::object_motion

#endif  // SRC_GAMES_TWWARHAMMER3_OBJECT_MOTION_HPP_
