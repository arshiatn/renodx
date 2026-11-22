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

// VFX Brightness (Attila's vfx_base_brightness, Troy's twtroy/vfx_brightness.hpp): the colour of the game's effects x the
// slider. HASHES: every forward pixel shader whose material sources all sit under Materials/Effects (the census), less
// those that read the scene's colour (_scene: refraction, heat haze, shields, water; scaling them would brighten what
// is behind them), those lit by the sun's shadows and SSAO (body parts, blood, ooze, lava: effect surfaces) and the
// add-on's own fire 0xED93F883 and particles 0x93AEBB93 (their shaders scale by shared.h's VFX_BRIGHTNESS):
// tools\tw\testbin\scratch\g06_\vfx_classes.py. Each writes only o0, through one ret. They are patched as the game
// creates them: every write of o0 goes to one temporary more, and before each ret mad o0.xyz, rT.xyzx, cb7[0].xxxx,
// rT.xyzx and mov o0.w, rT.wwww, cb7 declared (they read cb0, cb12, cb13). The addon binds cb7 on each of their draws
// with the slider minus 1 and puts the draw's own back after it. At 1 the game's colour, to the bit (x 0 + x).
// test\tw_d1_vfx: each patched shader against the game's on WARP.
namespace divinity::vfx_brightness {

using Microsoft::WRL::ComPtr;

constexpr std::array<uint32_t, 258> HASHES = {
    0x00BEDCD1, 0x01B02235, 0x03F0EA4E, 0x066EE300, 0x06E4A03B, 0x08513723, 0x09C82251, 0x09F55631,
    0x0BF0309F, 0x0DF5AC5D, 0x0EBC9DC6, 0x0EF15815, 0x0EF61F29, 0x0FA65171, 0x1143E783, 0x11577DB2,
    0x121F9EC0, 0x1264CB8D, 0x135FD960, 0x13754CAD, 0x161E9885, 0x164AB4ED, 0x17C0428B, 0x180D01AD,
    0x186A66F9, 0x18EB64B9, 0x1A572C8D, 0x1B26FA67, 0x1C349D56, 0x1D1C7B6D, 0x1F2EE50A, 0x1F8DFBB5,
    0x2132F807, 0x213E3F43, 0x221FCA75, 0x2251BEFC, 0x2262B418, 0x22E8BDD7, 0x24BCCD25, 0x25EE36A4,
    0x27B9DDE9, 0x2A148BF6, 0x2B43A99F, 0x2B52DC45, 0x2B5488E6, 0x2B57FE7D, 0x2CC31D21, 0x2EE7AE3F,
    0x2F7F1553, 0x30F2C6EA, 0x3109C985, 0x3197E2CD, 0x33472BDF, 0x3729016F, 0x38B08AD1, 0x3910B89A,
    0x396620C4, 0x3C5856FF, 0x3CB9122D, 0x3D202378, 0x3F3F52FC, 0x40DA4BE5, 0x42C40729, 0x431D1AC5,
    0x4345AEAD, 0x45194B91, 0x460E0BAA, 0x4715ED85, 0x47C1F2A0, 0x48D5100B, 0x490162D4, 0x495040D7,
    0x4A2E6E6B, 0x4BC9B66C, 0x4CC8477D, 0x4DDCB3F2, 0x4F683863, 0x52315300, 0x530A4091, 0x53911748,
    0x53A2F6F1, 0x53AD9273, 0x5429F648, 0x54A8D78B, 0x54DFFE7D, 0x56D9E543, 0x5755FC3A, 0x5A26F60B,
    0x5B10B490, 0x5C40BE6F, 0x5C6F5503, 0x5E1A3773, 0x5FD30020, 0x607F11E6, 0x60AD52F5, 0x63BC1224,
    0x63C34BB8, 0x6560024F, 0x665C9D56, 0x672BFE70, 0x6793C28F, 0x69034D64, 0x69057E9E, 0x6924469D,
    0x6C03431E, 0x6CCF74F5, 0x707469E3, 0x713A6F29, 0x737FC5A9, 0x73DB6DA3, 0x7447EBA3, 0x7611F762,
    0x7701D8EF, 0x775DCC67, 0x7A67425C, 0x7B39AD7E, 0x7F2BDAFE, 0x7FCE6422, 0x80327826, 0x8121A6BC,
    0x823E6959, 0x83AD9CE4, 0x846AA631, 0x8572F5D5, 0x86D4A43E, 0x87D82CC9, 0x87E47E82, 0x8956DE81,
    0x8AB39209, 0x8B225D01, 0x8B475894, 0x8BC0D2B8, 0x8DEB833C, 0x8F0681B0, 0x93B6D904, 0x944E3783,
    0x953C6F5B, 0x96C91B5E, 0x96F38B57, 0x98250726, 0x983E7193, 0x98F32585, 0x9916586C, 0x992670CA,
    0x9943F9DE, 0x99A438A6, 0x9A70ED0F, 0x9BB82FFD, 0x9C1A6A73, 0x9D4BCC46, 0x9DBE8587, 0x9EA1E74B,
    0x9FCDCFED, 0x9FDFA4EE, 0xA1C124BF, 0xA225C0D3, 0xA276D735, 0xA2AD80ED, 0xA2CF6203, 0xA312D26A,
    0xA3CF4F38, 0xA4484571, 0xA5B24C40, 0xA5ECBDD8, 0xA6F47AB7, 0xA7952746, 0xA99AEAD1, 0xA9DD4394,
    0xAA7F1DD3, 0xAABB0EE6, 0xAB2159F5, 0xAB3C07C1, 0xAD262D1E, 0xADBD3E7E, 0xAE13143D, 0xAE5135F2,
    0xAF6D007D, 0xAF849924, 0xAFB58362, 0xB0113C73, 0xB03ABDD4, 0xB1397BD8, 0xB2A97E7C, 0xB3D2B809,
    0xB573DDDA, 0xB8166C06, 0xB9955DC0, 0xBA711B2A, 0xBAE930D7, 0xBB5C0FDB, 0xBB900FFC, 0xBBE41B74,
    0xBD70A06E, 0xBDEF8640, 0xBEFE5192, 0xC03379EC, 0xC05AF7CC, 0xC2438F34, 0xC32411CB, 0xC34145D2,
    0xC3D3F565, 0xC6678752, 0xC7219CA0, 0xC79698F1, 0xC825476C, 0xC87C064F, 0xC8866E03, 0xC8E097C4,
    0xCB6765F9, 0xCC36BA86, 0xCCA19A3D, 0xCCBD3B3C, 0xCCF9FCD8, 0xCF0074BF, 0xCF8E0BB6, 0xD018E807,
    0xD03A4540, 0xD088384F, 0xD276027D, 0xD4320F1E, 0xD4B28348, 0xD749BA8F, 0xD7DE1E2A, 0xD807949F,
    0xD9015809, 0xDAFDAF0D, 0xDB0BDC21, 0xDBBBCE8C, 0xDC1E08E6, 0xE0B70D83, 0xE2772B89, 0xE4683CA9,
    0xE769F5DC, 0xE7BE1A32, 0xE8D51932, 0xE8D8867E, 0xEAAB355A, 0xEAE2E916, 0xEC12DB2F, 0xECE26BAE,
    0xECF59E0E, 0xEEECCBE9, 0xEFCE8110, 0xF0F8F347, 0xF1B70091, 0xF3C42FD0, 0xF47883B9, 0xF662FA24,
    0xF68F8714, 0xF69D0776, 0xF6BBF0A7, 0xF75FFB16, 0xF972E391, 0xF9CDA6A9, 0xFB579433, 0xFBD7EFA9,
    0xFBFC10F6, 0xFCC74130,
};
constexpr UINT SLOT = 7;

// `code` with the brightness before each ret. Empty: not a pixel shader with dcl_temps, cb7 declared already, a retc,
// or an operand it cannot read.
inline std::vector<uint8_t> Patch(std::span<const uint8_t> code) {
  constexpr uint32_t OP_MAD = 50, OP_MOV = 54, OP_RET = 62, OP_RETC = 63, OP_DCL_CONSTANT_BUFFER = 89;
  constexpr uint32_t TEMP = 0, OUTPUT = 2;
  twattila::dxbc::Program program = twattila::dxbc::Read(code);
  auto& tokens = program.tokens;
  // The version token: the program type from bit 16 (0: pixel).
  if (tokens.empty() || (tokens[0] >> 16) != 0) return {};
  size_t temps_at = 0;
  const size_t first_instruction = twattila::dxbc::internal::FirstInstruction(tokens, &temps_at);
  if (first_instruction == 0 || temps_at == 0) return {};
  for (size_t at = 2; at < first_instruction; at += twattila::dxbc::Length(tokens, at)) {
    if ((tokens[at] & 0x7FF) == OP_DCL_CONSTANT_BUFFER && tokens[at + 2] == SLOT) return {};
  }
  const uint32_t temp = tokens[temps_at];
  // Every output operand (o0 is the only output) into rT: its type to a temporary, its index to T.
  std::vector<size_t> rets;
  for (size_t at = first_instruction; at < tokens.size();) {
    const size_t length = twattila::dxbc::Length(tokens, at);
    if (length == 0) return {};
    const uint32_t opcode = tokens[at] & 0x7FF;
    if (opcode == OP_RETC) return {};
    if (opcode == OP_RET) rets.push_back(at);
    for (size_t operand_at = (length > 1 ? twattila::dxbc::internal::FirstOperand(tokens, at) : at + length); operand_at < at + length;) {
      twattila::dxbc::Operand operand;
      const size_t operand_length = twattila::dxbc::ParseOperand(tokens, operand_at, at + length, &operand);
      if (operand_length == 0) return {};
      if (operand.type == OUTPUT) {
        // o0 itself: one immediate index (0), no extended operand token.
        if (((tokens[operand_at] >> 20) & 3) != 1 || ((tokens[operand_at] >> 22) & 7) != 0 || (tokens[operand_at] >> 31) != 0
            || operand.index[0] != 0) {
          return {};
        }
        tokens[operand_at] = (tokens[operand_at] & ~(0xFFu << 12)) | (TEMP << 12);
        tokens[operand_at + 1] = temp;
      }
      operand_at += operand_length;
    }
    at += length;
  }
  if (rets.empty()) return {};
  tokens[temps_at] = temp + 1;
  // o0.xyz, rT.xyzx, cb7[0].xxxx (2D immediate index), o0.w, rT.wwww; dcl_constantbuffer cb7[1], immediateIndexed.
  constexpr uint32_t OUTPUT_XYZ = 2u | (0x7u << 4) | (OUTPUT << 12) | (1u << 20);
  constexpr uint32_t OUTPUT_W = 2u | (0x8u << 4) | (OUTPUT << 12) | (1u << 20);
  constexpr uint32_t TEMP_XYZX = 2u | (1u << 2) | (0x24u << 4) | (TEMP << 12) | (1u << 20);
  constexpr uint32_t TEMP_WWWW = 2u | (1u << 2) | (0xFFu << 4) | (TEMP << 12) | (1u << 20);
  constexpr uint32_t CONSTANT_XXXX = 2u | (1u << 2) | (8u << 12) | (2u << 20);
  const std::array<uint32_t, 15> brightness = {OP_MAD | (10u << 24), OUTPUT_XYZ, 0, TEMP_XYZX, temp, CONSTANT_XXXX, SLOT, 0, TEMP_XYZX, temp,
                                               OP_MOV | (5u << 24), OUTPUT_W, 0, TEMP_WWWW, temp};
  for (auto ret = rets.rbegin(); ret != rets.rend(); ++ret) {
    tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(*ret), brightness.begin(), brightness.end());
  }
  // Before dcl_temps (the declarations come first; temps_at is its count's token).
  constexpr std::array<uint32_t, 4> DECLARATION = {0x04000059, 0x00208E46, SLOT, 1};
  tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(temps_at - 1), DECLARATION.begin(), DECLARATION.end());
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
    const uint32_t hash = renodx::utils::hash::ComputeCRC32(code.data(), code.size());
    if (std::ranges::find(HASHES, hash) == HASHES.end()) continue;
    const std::unique_lock lock(runtime.mutex);
    auto [entry, added] = runtime.patched.try_emplace(hash);
    if (added) {
      entry->second = Patch(code);
      std::stringstream s;
      s << "[Divinity] VFX Brightness: effect shader " << PRINT_CRC32(hash) << (entry->second.empty() ? " not patched." : " patched.");
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

// An effect's draw: the slider minus 1 at cb7 for it, the draw's own put back after it.
constexpr auto OnEffectDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
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
    if (!runtime.buffer) return {};
    if (runtime.uploaded != runtime.brightness) {
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
        OnEffectDraw, {.shader_hash = hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
}

inline void Uninstall() {
  renodx::utils::pipeline::UnregisterOnCreateCallback(internal::OnCreatePipeline);
  renodx::utils::command_action::Unregister(OnEffectDraw);
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

}  // namespace divinity::vfx_brightness
