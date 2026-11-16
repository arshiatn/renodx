// twattila/ao_readers.hpp on WARP with ReShade's command list mocked: Pharaoh's building pools (its AO_READERS, the four
// builds' t_ssao slots from their bytes) under Ray-Traced AO.
// 1. This frame's combine ran from the game's AO now at the pool's slot: the pool's draw reads rt_scene's combined texture
//    there (the rays' AO in red: a draw of the bound view returns it), the game's view is bound again after the draw and
//    its reference count is back where it was.
// 2. CONTROLS (the game's view stays, nothing to bind back): last frame's combine; another texture at the slot (not the one
//    the combine read); nothing bound; no combined texture; ray tracing inactive. And the CONTROL that must fail: with
//    the hook skipped the slot reads the game's AO, not the rays'.
// 3. The pools' own bytes (the game's dump, argv[1]): each binds its t_ssao at the slot AO_READERS names, and samples it
//    (fxc keeps only the textures a shader reads in its reflection); CONTROL: their t_cloud_shadow, another slot.
//    Their pixels from that read were not shown: tw_vanilla_exact's noise frame carries none of the slot to their outputs
//    (with the battle path's g_campaign_flat_map 0 and the cube array bound, 0 / 0.37 / 100 there gave the same bits).
// tests.exe <the Pharaoh dump's cso folder>
#include <windows.h>
#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <cstdio>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "src/games/twattila/ao_readers.hpp"
#include "test/tw_particle_history/mock_reshade.hpp"

using Microsoft::WRL::ComPtr;
namespace core = twattila::rt_scene;
namespace action = renodx::utils::command_action;
namespace readers = twattila::ao_readers;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

extern "C" __declspec(dllexport) const char* const NAME = "AO readers test";
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}

// Pharaoh's (src/games/twpharaohdynasties/addon.cpp AO_READERS), with each build's shader model in the dump.
constexpr std::array<readers::AoReader, 4> AO_READERS = {{
    {.shader_hash = 0x9D28319F, .ao_slot = 12},
    {.shader_hash = 0x03B9CEFE, .ao_slot = 12},
    {.shader_hash = 0x40154AD7, .ao_slot = 9},
    {.shader_hash = 0xAC04E623, .ao_slot = 11},
}};
constexpr std::array<const char*, 4> SHADER_MODELS = {"ps_5_0", "ps_4_1", "ps_5_0", "ps_5_0"};

// The slot a shader's bytes bind `name` at (its reflection), else UINT_MAX.
static UINT TextureSlot(const std::string& path, const char* name) {
  std::ifstream file(path, std::ios::binary);
  const std::vector<char> code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  ComPtr<ID3D11ShaderReflection> reflection;
  if (code.empty() || FAILED(D3DReflect(code.data(), code.size(), IID_PPV_ARGS(&reflection)))) return UINT_MAX;
  D3D11_SHADER_INPUT_BIND_DESC bind = {};
  if (FAILED(reflection->GetResourceBindingDescByName(name, &bind)) || bind.Type != D3D_SIT_TEXTURE) return UINT_MAX;
  return bind.BindPoint;
}

constexpr UINT TEXTURE_SIZE = 16;

static ComPtr<ID3D11ShaderResourceView> Texture(ID3D11Device* device, float red) {
  std::vector<float> texels(size_t{TEXTURE_SIZE} * TEXTURE_SIZE * 4);
  for (size_t texel = 0; texel < texels.size(); texel += 4) texels[texel] = red, texels[texel + 1] = 0.5f, texels[texel + 2] = 0.6f, texels[texel + 3] = 0.7f;
  const D3D11_TEXTURE2D_DESC desc = {TEXTURE_SIZE, TEXTURE_SIZE, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
  const D3D11_SUBRESOURCE_DATA init = {texels.data(), TEXTURE_SIZE * 16, 0};
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> view;
  if (FAILED(device->CreateTexture2D(&desc, &init, &texture)) || FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &view))) return {};
  return view;
}

static ULONG References(IUnknown* object) {
  object->AddRef();
  return object->Release();
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: tests.exe <the Pharaoh dump's cso folder>\n");
    return 2;
  }
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> immediate;
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &immediate)));
  mock::Device game_device;
  game_device.native = device;
  mock::CommandList cmd_list;
  cmd_list.owner = &game_device;
  cmd_list.native = immediate;

  // The game's AO (red 0.2), rt_scene's combined texture from it (the rays' AO in red: 0.9, the game's other channels),
  // another texture of the game's (red 0.4).
  const auto game_ao = Texture(device.Get(), 0.2f);
  const auto combined = Texture(device.Get(), 0.9f);
  const auto other = Texture(device.Get(), 0.4f);
  CHECK(game_ao && combined && other);

  // A draw of the slot's view: one pixel, its red (the pool's own t_ssao read is run.ps1's).
  const auto read_slot = [&](UINT slot) -> float {
    const std::string source = "Texture2D<float4> t : register(t" + std::to_string(slot) + "); float4 main(float4 p : SV_Position) : SV_Target { return t.Load(int3(3, 5, 0)); }";
    const std::string vertex = "float4 main(uint id : SV_VertexID) : SV_Position { float2 uv = float2((id << 1) & 2, id & 2); return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); }";
    ComPtr<ID3DBlob> ps_code;
    ComPtr<ID3DBlob> vs_code;
    if (FAILED(D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &ps_code, nullptr))
        || FAILED(D3DCompile(vertex.data(), vertex.size(), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vs_code, nullptr))) return -1.f;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11VertexShader> vs;
    device->CreatePixelShader(ps_code->GetBufferPointer(), ps_code->GetBufferSize(), nullptr, &ps);
    device->CreateVertexShader(vs_code->GetBufferPointer(), vs_code->GetBufferSize(), nullptr, &vs);
    const D3D11_TEXTURE2D_DESC desc = {1, 1, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> rtv;
    device->CreateTexture2D(&desc, nullptr, &target);
    device->CreateRenderTargetView(target.Get(), nullptr, &rtv);
    D3D11_TEXTURE2D_DESC staging_desc = desc;
    staging_desc.Usage = D3D11_USAGE_STAGING, staging_desc.BindFlags = 0, staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    device->CreateTexture2D(&staging_desc, nullptr, &staging);
    const D3D11_VIEWPORT viewport = {0, 0, 1, 1, 0, 1};
    immediate->RSSetViewports(1, &viewport);
    immediate->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
    immediate->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    immediate->VSSetShader(vs.Get(), nullptr, 0);
    immediate->PSSetShader(ps.Get(), nullptr, 0);
    immediate->Draw(3, 0);
    immediate->CopyResource(staging.Get(), target.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(immediate->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return -1.f;
    const float red = *static_cast<const float*>(mapped.pData);
    immediate->Unmap(staging.Get(), 0);
    return red;
  };
  const auto bound = [&](UINT slot) {
    ComPtr<ID3D11ShaderResourceView> view;
    immediate->PSGetShaderResources(slot, 1, view.GetAddressOf());
    return view;
  };

  auto& runtime = core::internal::runtime;
  core::internal::active.store(true);
  const auto combine_ran = [&](bool this_frame, ID3D11ShaderResourceView* from) {
    const std::unique_lock lock(runtime.mutex);
    runtime.frame = 7;
    runtime.combined_frame = this_frame ? 7 : 6;
    runtime.game_ao = from;
    runtime.objects.combined_srv = combined;
  };

  // 1. Each pool under Ray-Traced AO.
  for (const auto& reader : AO_READERS) {
    combine_ran(true, game_ao.Get());
    ID3D11ShaderResourceView* game_view = game_ao.Get();
    immediate->PSSetShaderResources(reader.ao_slot, 1, &game_view);
    const ULONG references = References(game_ao.Get());
    action::CommandContext<action::DrawIndexedArguments> command = {.cmd_list = &cmd_list, .arguments = {.index_count = 48, .instance_count = 1},
                                                                     .callback_data = &reader};
    const auto result = readers::internal::OnAoReaderDraw.template operator()<readers::AoReader>(command);
    CHECK(bound(reader.ao_slot).Get() == combined.Get());
    CHECK(read_slot(reader.ao_slot) == 0.9f);
    CHECK(result.finalize_callback != nullptr && result.post_data == game_ao.Get());
    result.finalize_callback(command, result.post_data);
    CHECK(bound(reader.ao_slot).Get() == game_ao.Get());
    CHECK(References(game_ao.Get()) == references);
    std::printf("1 %08X t%u: the rays' AO during its draw (red %.2f), the game's AO bound again after, references %lu as before\n",
                reader.shader_hash, reader.ao_slot, 0.9f, references);
  }

  // 2. CONTROLS: the game's view stays.
  const auto stays = [&](const readers::AoReader& reader, ID3D11ShaderResourceView* at_slot) {
    immediate->PSSetShaderResources(reader.ao_slot, 1, &at_slot);
    action::CommandContext<action::DrawIndexedArguments> command = {.cmd_list = &cmd_list, .callback_data = &reader};
    const auto result = readers::internal::OnAoReaderDraw.template operator()<readers::AoReader>(command);
    return result.finalize_callback == nullptr && result.post_data == nullptr && bound(reader.ao_slot).Get() == at_slot;
  };
  const auto& pool = AO_READERS[0];
  combine_ran(false, game_ao.Get());
  CHECK(stays(pool, game_ao.Get()));
  combine_ran(true, game_ao.Get());
  CHECK(stays(pool, other.Get()));
  CHECK(stays(pool, nullptr));
  {
    const std::unique_lock lock(runtime.mutex);
    runtime.objects.combined_srv.Reset();
  }
  CHECK(stays(pool, game_ao.Get()));
  combine_ran(true, game_ao.Get());
  core::internal::active.store(false);
  CHECK(stays(pool, game_ao.Get()));
  core::internal::active.store(true);
  // The CONTROL that must fail: without the hook the pool reads the game's AO (0.2), not the rays' (0.9).
  ID3D11ShaderResourceView* game_view = game_ao.Get();
  immediate->PSSetShaderResources(pool.ao_slot, 1, &game_view);
  const float without_hook = read_slot(pool.ao_slot);
  CHECK(without_hook == 0.2f);
  std::printf("2 last frame's combine, another texture, none, no combined texture, inactive: the game's view stays; CONTROL without the hook: red %.2f (the game's)\n",
              without_hook);
  {
    const std::unique_lock lock(runtime.mutex);
    runtime.objects.combined_srv.Reset();
    runtime.game_ao = nullptr;
  }

  // 3. The pools' bytes: t_ssao at the listed slot; CONTROL: one off is not.
  for (size_t i = 0; i < AO_READERS.size(); ++i) {
    const auto& reader = AO_READERS[i];
    const std::string path = std::format("{}\\0x{:08X}.{}.cso", argv[1], reader.shader_hash, SHADER_MODELS[i]);
    const UINT slot = TextureSlot(path, "t_ssao");
    const UINT cloud_slot = TextureSlot(path, "t_cloud_shadow");
    CHECK(slot == reader.ao_slot);
    CHECK(cloud_slot != UINT_MAX && cloud_slot != reader.ao_slot);
    std::printf("3 %08X (%s): its bytes sample t_ssao at t%u, as listed; CONTROL: their t_cloud_shadow at t%u\n", reader.shader_hash,
                SHADER_MODELS[i], slot, cloud_slot);
  }
  std::printf("PASS\n");
  return 0;
}
