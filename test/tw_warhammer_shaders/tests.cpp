// Total War: WARHAMMER's sun light and screen-space reflection shaders (src/games/twwarhammer) on WARP.
// tests <repo root> <the game's shader dump folder (cso: 0x230F2241.ps_4_1.cso)>
// 1. The game's own sun light shader 230F2241 (directionallight slm highdef; its bytes, read only) at one G-buffer pixel:
//    normal +x, sun from +x, lit (no cascade, no cloud), no material flags, no specular colour, AO 1. Its diffuse light per
//    unit albedo and sun colour (two albedos' difference) with g_hdr_on 1 and 0, its ambient per unit albedo and cube +x;
//    WH1's rt_sun (its wrapper) on the same lighting buffer: texel 2 the sun colour x that diffuse factor (g_hdr_on 1),
//    texel 4's w the ambient's, texel 0 the way to the sun, texels 3-8 the cube. CONTROL: the shared file's defaults
//    (Attila's switch on: float 44 is g_hdr_on here) give 0.004 and 1.44, not the game's.
// 2. The SSR resolve replacement (shaders/ssr00_0x6B80E35B.cs_5_0.hlsl) at 100% on 64 x 64 targets and at 50% on 128 x 128
//    targets whose top-left 64 x 64 hold the same texels (cb12's frame share 0.5): the same output there with the march's
//    hit at the pixel (the position from the view's uv), and the march's offset (a hit 2.1 texels away at 100%) the same
//    direction (the projected uv in the targets' share; its length in uv as the game's patched bytes keep it).
//    CONTROLS: the source without the position's share, without the projected uv's share, and cb12 unbound at 50% differ.
// 3. The paths' sky: 230F2241's diffuse ambient per unit albedo at each of the six axis normals (its bytes, sun 0), with
//    the spherical harmonics on (a 9 x 6 texture: 3 x 2 cells; inside the playable area between cells, at its far edge where
//    the next cell wraps, outside it) and off (the cube), against WH1's rt_sun's texels 3-8 with the camera at that pixel's
//    place; their w (texel 3: 0 with the harmonics and Path-Traced AO off; texel 6: 0 with the harmonics). CONTROL: rt_sun
//    with the camera one cell over differs.
// 4. The ambient patch (screen_space_shadows.hpp's Patch, then PatchAmbient with AMBIENT_AFTER_HARMONICS_Z, as the runtime;
//    run.ps1 copies both out of the tree): found in all 31 of LIGHT_BOUNCE_DRAWS and their bytes create; the 11 single-
//    sampled drawn with the harmonics on: t51 unbound the game's bits, t51 = 1 the game's with its ambient black (harmonics
//    and cube 0), t51 = 0.5 halfway. CONTROL: t51 = 1 differs from unbound.
// 5. The shared PatchAmbient against run.ps1's -Before copy: every other game's LIGHT_BOUNCE_DRAWS (their tables, their
//    dumps beside this one) patch to the same bytes (Patch first, as the runtime). CONTROL: Warhammer's 31 differ. Patch
//    with its defaults (forward and not: every game's sun and forward rows) on every PS in the 8 dumps the same bytes as
//    before. CONTROL: with `declare_position` Warhammer's forward readers 0BF2DB7F / BCB9B1D6 patch (before: not).
// 6. The rays' new casters (the build's embed/*.cso): the far trees' cards (CA6BCF19) streamed out (their world, TEXCOORD2)
//    with the camera level-free along the way to the sun, against rt_capture_far_tree with rt_sun's texel 0 that way; the
//    campaign plants (84797031, no wind, the camera below them: no lean) streamed out (TEXCOORD0), against
//    rt_capture_campaign_vegetation (world and UV) and rt_rows_campaign_vegetation's rows x the vertex. CONTROLS: the camera
//    turned 90 degrees, the camera above the plants: they differ.
// 7. Torch and Fire Shadows' point light shaders (the build's embed/0x<hash>.cso, nothing at t42 / b9: the game's light)
//    against the game's D0C67938 / 5D1695BC on a 4 x 4 G-buffer, 8 lights each. CONTROL: the other falloff differs.
// 8. Skip the Shadow Maps' forward readers (screen_space_shadows.hpp Patch with `forward` and `declare_position`, as
//    PatchForwardShadows(..., true)): the 7 PS that read the atlas in ATN's traces, each drawn with every resource filled
//    and the shadow's constants by name (FORWARD_CONSTANTS): t49 unbound bit-identical to the game's bytes (atlas 0 and
//    1), t49 = 1 over a lit atlas equal to the game's with every comparison failing (atlas 0). CONTROL: the atlas's value
//    changes the game's output.
// 9. Ray Reconstruction's guides (twwarhammer/aa_rr_guides: Warhammer II's file) against 230F2241's own decode of the same
//    G-buffer: 6 oblique normals (t5's two angles, gloss 0.3) lit from +x, +y and +z (sun 1, no ambient, no specular
//    colour): the game's diffuse per unit albedo over its length the guides' normal; the game's ambient per unit albedo
//    (every cube side 1) with specular colour 0.3 and 0 the guides' diffuse albedo over the albedo (1 - specular colour);
//    their roughness 1 - gloss. CONTROLS: the angles read the other way, the diffuse albedo without the specular colour.
// 10. Light Through All Banners: the banners' vertex shaders (rigidtextureblend DBCC30FD, its 2 bones E7F0D71A) output
//    SV_Position first (register 0), which sss_thin reads; Coloured Shadows' albedo_power 1: 230F2241's diffuse is linear in
//    channel 2's colour (its slope from 0.1 to 0.3 the one from 0.3 to 0.9; a squared colour makes it 3 times). CONTROL:
//    the imposter's vertex shader 381E079E (POSITION first) fails the first.
// 11. Light Through Foliage's bit (foliage_material_bit 128): the leaves' G-buffer shader 744FDFF0 (its bytes, drawn as part
//    8 draws, the camera far and its inputs' w 0: no fade) writes channel 3's alpha 128 / 255; 230F2241 lit from behind (sun
//    1 along the normal, no ambient) lets light through only with that bit. CONTROLS: the branches' 4CF47121 writes 0;
//    flags 64 (water) and 16 let none through.
// 12. Water Clarity and Caustics on the battle water 44B4A2F0 (water_effects.hpp, WH1's WATER_SHADERS row): water_clarity.cs
//    leaves every dword of a lighting_VS_PS copy but c10 (g_sea_bed_light_scatter) bit-identical and raises those to 1 /
//    clarity; the game's bytes with that c10 a metre deep equal its own half a metre deep (clarity 2; a flat surface, the
//    depth buffer's ground by name). PatchWaterCaustics (on the game's bytes and on part 8's forward patch, as the runtime
//    chains them): t51 unbound bit-identical. CONTROLS: the game's c10 a metre deep differs; t51 = 1 differs.
// 13. Cut-outs on More Models: the units' capture versions with the UV (the build's embed/rt_capture_unit_cut_out.cso and
//    its 4 bones', RT_CAPTURE_UV) stream out in TEXCOORD0's w the UV the game's 20A9E5FD / 3FC5424C give at TEXCOORD1 (what
//    their pixel shaders' alpha test reads, tools/tw/wh1/cut_out_census_wh1.txt), as two halves. CONTROL: the captures
//    without the UV.
// 14. Skip the Game's Reflection Passes: under the rays' reflections (t47 bound) the resolve replacement (part 2's) writes
//    bit-identical texels whatever the march's hits (t5) and the scene's light mips (t6) hold, so their passes go with the
//    march (tools/tw/wh1/reader_census_wh1.txt: nothing else reads them); without the rays it writes what the game's own
//    6B80E35B does. CONTROLS: without the rays, ours and the game's bytes change with those inputs.
// 15. Rays' AO Alone: the game's AO target (R8_UNORM, as traced) cleared to open by ClearUnorderedAccessViewFloat gives
//    rt_ambient_combine (twattila, without RT_COMBINE_SUN_MAPPING as here) the traced visibility alone; the sun shaders
//    (CF0C5118 drawn in ATN's battles, 230F2241: their bytes, ambient cube light) read t_ssao's red only. CONTROLS: the
//    game's AO at 0.5 changes the combine; the sun's light follows t_ssao's red.
// 16. The battle fog (shaders/battlefog00, the build's embed/0xE684D54E.cso; the game's E684D54E drawn in 34 of ATN's 36
//    traces) over 8 x 8 pixels of varied depth, its constants by name in the game's ranges (height, distance, clear-distance
//    and forced fog all partial): Fog Strength 1 equals the game's bytes (colour and opacity); 0.5 halves the opacity and
//    keeps the colour. CONTROL: Fog Strength 0.5 differs from the game's opacity.
#include <windows.h>
#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <span>
#include <string>
#include <vector>

#include "ambient_patch.h"
// The add-on's settings buffer (b13) as the shaders read it.
#include "../../src/games/twwarhammer/shared.h"

using Microsoft::WRL::ComPtr;

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;
static int failures = 0;

#define CHECK(x, ...)                                         \
  do {                                                        \
    if (!(x)) {                                               \
      std::printf("FAIL line %d: %s: ", __LINE__, #x);        \
      std::printf(__VA_ARGS__);                               \
      std::printf("\n");                                      \
      ++failures;                                             \
    }                                                         \
  } while (false)

using Float4 = std::array<float, 4>;

static std::string ReadText(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::printf("cannot read %s\n", path.c_str());
    std::exit(2);
  }
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static ComPtr<ID3DBlob> CompileText(const std::string& source, const std::string& name, const char* entry, const char* target,
                                    const D3D_SHADER_MACRO* macros = nullptr) {
  ComPtr<ID3DBlob> code;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompile(source.data(), source.size(), name.c_str(), macros, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, target,
                        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.GetAddressOf(), errors.GetAddressOf()))) {
    std::printf("compile failed (%s): %s\n", name.c_str(), errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
    std::exit(2);
  }
  return code;
}

static ComPtr<ID3D11ComputeShader> ComputeShader(const std::string& source, const std::string& name, const D3D_SHADER_MACRO* macros = nullptr) {
  const auto code = CompileText(source, name, "main", "cs_5_0", macros);
  ComPtr<ID3D11ComputeShader> shader;
  device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf());
  return shader;
}

// The source with `from` replaced by `to` (once; it must be there).
static std::string Without(std::string source, const std::string& from, const std::string& to) {
  const auto at = source.find(from);
  if (at == std::string::npos) {
    std::printf("control text not found: %s\n", from.c_str());
    std::exit(2);
  }
  return source.replace(at, from.size(), to);
}

static ComPtr<ID3D11Buffer> Constants(const void* data, UINT bytes) {
  const D3D11_BUFFER_DESC desc = {.ByteWidth = bytes, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = data};
  ComPtr<ID3D11Buffer> buffer;
  device->CreateBuffer(&desc, &initial, buffer.GetAddressOf());
  return buffer;
}

static ComPtr<ID3D11Texture2D> Texture(UINT width, UINT height, const void* data, UINT bind, UINT array_size = 1, UINT misc = 0) {
  const D3D11_TEXTURE2D_DESC desc = {.Width = width, .Height = height, .MipLevels = 1, .ArraySize = array_size, .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
                                     .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = bind, .MiscFlags = misc};
  std::vector<D3D11_SUBRESOURCE_DATA> initial(array_size);
  for (UINT slice = 0; slice < array_size; ++slice) {
    initial[slice] = {.pSysMem = static_cast<const uint8_t*>(data) + (static_cast<size_t>(slice) * width * height * 16), .SysMemPitch = width * 16};
  }
  ComPtr<ID3D11Texture2D> texture;
  device->CreateTexture2D(&desc, (data != nullptr) ? initial.data() : nullptr, texture.GetAddressOf());
  return texture;
}

static ComPtr<ID3D11ShaderResourceView> View(UINT width, UINT height, const std::vector<Float4>& texels) {
  ComPtr<ID3D11ShaderResourceView> view;
  device->CreateShaderResourceView(Texture(width, height, texels.data(), D3D11_BIND_SHADER_RESOURCE).Get(), nullptr, view.GetAddressOf());
  return view;
}

static std::vector<Float4> Read(ID3D11Texture2D* texture) {
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf());
  context->CopyResource(staging.Get(), texture);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  std::vector<Float4> texels(static_cast<size_t>(desc.Width) * desc.Height);
  for (UINT row = 0; row < desc.Height; ++row) {
    std::memcpy(texels.data() + (static_cast<size_t>(row) * desc.Width), static_cast<const uint8_t*>(mapped.pData) + (static_cast<size_t>(row) * mapped.RowPitch),
                static_cast<size_t>(desc.Width) * 16);
  }
  context->Unmap(staging.Get(), 0);
  return texels;
}

static bool Near(float a, float b, float relative) {
  return std::fabs(a - b) <= (relative * std::max(std::fabs(a), std::fabs(b))) + 1e-30f;
}

// The game's sun shader's lighting_VS_PS (18 registers): sun_direction c0.yzw, sun_colour c1, the ambient cube c2-c7 (+x, -x,
// +y, -y, +z, -z), g_hdr_on c11.x, g_use_spherical_harmonics c11.z (0: the cube), the cloud uv's scale c12.w.
struct Lighting {
  std::array<float, 72> c = {};
};

static Lighting SunLighting(const Float4& sun_colour, const Float4& cube_x, float hdr_on) {
  Lighting lighting;
  auto& c = lighting.c;
  c[1] = -1.f;  // the sun shines along -x: the way to it +x
  std::copy_n(sun_colour.begin(), 3, c.begin() + 4);
  std::copy_n(cube_x.begin(), 3, c.begin() + 8);
  for (size_t side = 1; side < 6; ++side) c[8 + (side * 4)] = 0.01f * static_cast<float>(side);  // the other sides, faced away
  c[44] = hdr_on;
  c[51] = 1.f;
  return lighting;
}

// One G-buffer pixel for a game's sun shader: (1, 1) of 4 x 4, its world place (-0.25, 0.25, depth) (the camera's inverse
// view-projection identity); normal from t5's two angles, the albedo, AO 1; the harmonics texture; t51 (-1: unbound).
// Everything else unbound (0: no specular colour, no material flags, no cloud, no cascade, no environment).
struct SunScene {
  Lighting lighting;
  float albedo = 0.5f;
  std::array<float, 2> normal = {0.5f, 0.5f};
  float gloss = 0.f;
  float specular = 0.f;  // channel 3's specular colour
  float flags = 0.f;     // channel 3's alpha byte (the material's flags)
  float depth = 0.5f;
  UINT harmonics_width = 0;
  UINT harmonics_height = 0;
  std::vector<Float4> harmonics = {};
  float ambient_off = -1.f;
  Float4 ao = {1.f, 1.f, 1.f, 1.f};  // t_ssao
};

static ComPtr<ID3D11VertexShader> full_screen;

// The shader's bytes drawn over the scene, its resources bound by their names in its reflection.
static Float4 DrawGame(std::span<const uint8_t> code, const SunScene& scene) {
  ComPtr<ID3D11PixelShader> shader;
  if (FAILED(device->CreatePixelShader(code.data(), code.size(), nullptr, shader.GetAddressOf()))) return {NAN, NAN, NAN, NAN};
  ComPtr<ID3D11ShaderReflection> reflection;
  D3DReflect(code.data(), code.size(), IID_ID3D11ShaderReflection, reinterpret_cast<void**>(reflection.GetAddressOf()));
  std::array<float, 40 * 4> camera = {};
  camera[12 * 4 + 2] = 1.f;  // the view depth: z
  for (size_t row = 0; row < 4; ++row) camera[(21 + row) * 4 + row] = 1.f;  // inverse view-projection: identity
  camera[0 * 4 + 2] = -10.f;                                               // the camera behind
  for (const size_t reg : {27u, 29u}) {
    camera[reg * 4 + 0] = 4.f;
    camera[reg * 4 + 1] = 4.f;
    camera[reg * 4 + 2] = 0.25f;
    camera[reg * 4 + 3] = 0.25f;
  }
  const std::array<float, 4> colorimetry = {0.f, 1.f, 0.f, 0.f};
  std::vector<ComPtr<ID3D11Buffer>> buffers;
  std::vector<ComPtr<ID3D11ShaderResourceView>> views;
  D3D11_SHADER_DESC desc = {};
  reflection->GetDesc(&desc);
  for (UINT index = 0; index < desc.BoundResources; ++index) {
    D3D11_SHADER_INPUT_BIND_DESC bind = {};
    reflection->GetResourceBindingDesc(index, &bind);
    const std::string name = bind.Name;
    if (bind.Type == D3D_SIT_CBUFFER) {
      D3D11_SHADER_BUFFER_DESC buffer_desc = {};
      reflection->GetConstantBufferByName(bind.Name)->GetDesc(&buffer_desc);
      std::vector<uint8_t> data((buffer_desc.Size + 15) & ~15u);
      const auto fill = [&](const void* source, size_t bytes) { std::memcpy(data.data(), source, std::min(bytes, data.size())); };
      if (name == "camera") fill(camera.data(), sizeof(camera));
      if (name == "colorimetry_VS_PS") fill(colorimetry.data(), sizeof(colorimetry));
      if (name == "lighting_VS_PS") fill(scene.lighting.c.data(), sizeof(scene.lighting.c));
      buffers.push_back(Constants(data.data(), static_cast<UINT>(data.size())));
      context->PSSetConstantBuffers(bind.BindPoint, 1, buffers.back().GetAddressOf());
    } else if (bind.Type == D3D_SIT_TEXTURE) {
      if (name == "t_ssao") views.push_back(View(1, 1, {scene.ao}));
      else if (name == "gbuffer_channel_1_texture") views.push_back(View(4, 4, std::vector<Float4>(16, Float4{scene.normal[0], scene.normal[1], scene.gloss, 0.f})));
      else if (name == "gbuffer_channel_2_texture") views.push_back(View(4, 4, std::vector<Float4>(16, Float4{scene.albedo, scene.albedo, scene.albedo, 0.f})));
      else if (name == "gbuffer_channel_3_texture") views.push_back(View(4, 4, std::vector<Float4>(16, Float4{scene.specular, scene.specular, scene.specular, scene.flags / 255.f})));
      else if (name == "gbuffer_channel_4_texture") views.push_back(View(4, 4, std::vector<Float4>(16, Float4{scene.depth, 0.f, 0.f, 0.f})));
      else if (name == "t_world_spherical_harmonics" && !scene.harmonics.empty()) views.push_back(View(scene.harmonics_width, scene.harmonics_height, scene.harmonics));
      else continue;
      context->PSSetShaderResources(bind.BindPoint, 1, views.back().GetAddressOf());
    }
  }
  if (scene.ambient_off >= 0.f) {
    views.push_back(View(1, 1, {Float4{scene.ambient_off, scene.ambient_off, scene.ambient_off, scene.ambient_off}}));
    context->PSSetShaderResources(ambient_patch::AMBIENT_SLOT, 1, views.back().GetAddressOf());
  }
  const auto target = Texture(4, 4, nullptr, D3D11_BIND_RENDER_TARGET);
  ComPtr<ID3D11RenderTargetView> rtv;
  device->CreateRenderTargetView(target.Get(), nullptr, rtv.GetAddressOf());
  context->VSSetShader(full_screen.Get(), nullptr, 0);
  context->PSSetShader(shader.Get(), nullptr, 0);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D11_VIEWPORT viewport = {.Width = 4.f, .Height = 4.f, .MaxDepth = 1.f};
  context->RSSetViewports(1, &viewport);
  context->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
  context->Draw(3, 0);
  context->ClearState();
  return Read(target.Get())[5];
}

static std::vector<uint8_t> GameBytes(const std::string& dump, uint32_t hash) {
  for (const char* profile : {"ps_4_0", "ps_4_1", "ps_5_0", "vs_4_0", "vs_4_1", "vs_5_0", "cs_5_0"}) {
    char name[64];
    std::snprintf(name, sizeof(name), "/0x%08X.%s.cso", hash, profile);
    std::ifstream file(dump + name, std::ios::binary);
    if (file) return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  }
  return {};
}

// The albedo's share of the output (two albedos' difference over it).
static Float4 PerAlbedo(std::span<const uint8_t> code, SunScene scene) {
  constexpr float ALBEDO_A = 0.2f;
  constexpr float ALBEDO_B = 0.6f;
  scene.albedo = ALBEDO_A;
  const auto a = DrawGame(code, scene);
  scene.albedo = ALBEDO_B;
  const auto b = DrawGame(code, scene);
  Float4 share = {};
  for (size_t channel = 0; channel < 4; ++channel) share[channel] = (b[channel] - a[channel]) / (ALBEDO_B - ALBEDO_A);
  return share;
}

// rt_sun's nine texels from a lighting buffer (rt_scene.hpp's binds: b1 the offsets, b2 the sun draw's camera, t0 its
// harmonics; `camera` the camera's place, `path_ambient` rt_sun.z).
static std::array<Float4, 9> RunSun(ID3D11ComputeShader* shader, const Lighting& lighting, const SunScene* harmonics = nullptr,
                                    const Float4& camera = {}, bool path_ambient = true) {
  // rt_scene.hpp's offsets: sun_direction 1, the ambient cube 8, the sun colour 4 (InstallSunDraws(LIGHT_BOUNCE_DRAWS, 8, 4)).
  const std::array<uint32_t, 8> offsets = {1, 8, path_ambient ? 1u : 0u, 0, 4, 0, 0, 0};
  const std::vector<Float4> initial(9, Float4{7.f, 7.f, 7.f, 7.f});
  const auto texture = Texture(9, 1, initial.data(), D3D11_BIND_UNORDERED_ACCESS);
  ComPtr<ID3D11UnorderedAccessView> uav;
  device->CreateUnorderedAccessView(texture.Get(), nullptr, uav.GetAddressOf());
  const std::array<ID3D11Buffer*, 3> buffers = {Constants(lighting.c.data(), sizeof(lighting.c)).Detach(), Constants(offsets.data(), 32).Detach(),
                                                Constants(camera.data(), 16).Detach()};
  ComPtr<ID3D11ShaderResourceView> view;
  if (harmonics != nullptr && !harmonics->harmonics.empty()) view = View(harmonics->harmonics_width, harmonics->harmonics_height, harmonics->harmonics);
  context->CSSetShader(shader, nullptr, 0);
  context->CSSetConstantBuffers(0, 3, buffers.data());
  context->CSSetShaderResources(0, 1, view.GetAddressOf());
  context->CSSetUnorderedAccessViews(0, 1, uav.GetAddressOf(), nullptr);
  context->Dispatch(1, 1, 1);
  context->ClearState();
  for (auto* buffer : buffers) buffer->Release();
  std::array<Float4, 9> texels = {};
  std::memcpy(texels.data(), Read(texture.Get()).data(), sizeof(texels));
  return texels;
}

static void TestSun(const std::string& games, const std::string& dump) {
  const auto bytes = GameBytes(dump, 0x230F2241);
  CHECK(!bytes.empty(), "the game's 230F2241");
  const std::string wrapper = games + "/twwarhammer/rt_sun.cs_5_0.hlsl";
  const auto sun = ComputeShader(ReadText(wrapper), wrapper);
  const std::string shared = games + "/twattila/rt_sun.cs_5_0.hlsl";
  const auto shared_defaults = ComputeShader(ReadText(shared), shared);
  const Float4 sun_colour = {84112.55f, 71577.57f, 49356.4f, 0.f};
  const Float4 cube_x = {0.4870255f, 0.5041848f, 0.5121719f, 0.f};
  std::array<float, 2> diffuse = {};
  for (const float hdr_on : {1.f, 0.f}) {
    const auto sun_share = PerAlbedo(bytes, {.lighting = SunLighting(sun_colour, {}, hdr_on)});
    const auto sky_share = PerAlbedo(bytes, {.lighting = SunLighting({}, cube_x, hdr_on)});
    std::array<float, 3> sun_factor = {};
    std::array<float, 3> sky_factor = {};
    for (size_t channel = 0; channel < 3; ++channel) {
      sun_factor[channel] = sun_share[channel] / sun_colour[channel];
      sky_factor[channel] = sky_share[channel] / cube_x[channel];
      CHECK(std::isfinite(sun_factor[channel]) && std::isfinite(sky_factor[channel]), "g_hdr_on %g: not finite", hdr_on);
    }
    const float expected = (hdr_on > 0.f) ? 0.0031831f : 0.318310f;
    for (size_t channel = 0; channel < 3; ++channel) {
      CHECK(Near(sun_factor[channel], expected, 1e-4f), "g_hdr_on %g: the game's sun %zu x %.8g (%.8g)", hdr_on, channel, sun_factor[channel], expected);
      CHECK(Near(sky_factor[channel], 1.f, 1e-4f), "g_hdr_on %g: the game's ambient %zu x %.8g", hdr_on, channel, sky_factor[channel]);
    }
    diffuse[(hdr_on > 0.f) ? 0 : 1] = sun_factor[0];
    std::printf("230F2241 g_hdr_on %g: diffuse x %.8g %.8g %.8g of the sun colour, ambient x %.6g %.6g %.6g of the cube\n", hdr_on, sun_factor[0],
                sun_factor[1], sun_factor[2], sky_factor[0], sky_factor[1], sky_factor[2]);
  }
  Lighting lighting = SunLighting(sun_colour, cube_x, 1.f);
  const auto texels = RunSun(sun.Get(), lighting);
  for (size_t channel = 0; channel < 3; ++channel) {
    CHECK(Near(texels[2][channel], sun_colour[channel] * diffuse[0], 1e-4f), "rt_sun sun %zu %.8g vs the game's %.8g", channel, texels[2][channel],
          sun_colour[channel] * diffuse[0]);
    CHECK(texels[3][channel] == cube_x[channel], "rt_sun cube +x %zu %g", channel, texels[3][channel]);
    for (size_t side = 1; side < 6; ++side) {
      CHECK(texels[3 + side][channel] == lighting.c[8 + (side * 4) + channel], "rt_sun cube %zu.%zu %g", side, channel, texels[3 + side][channel]);
    }
  }
  CHECK(texels[0][0] == 1.f && texels[0][1] == 0.f && texels[0][2] == 0.f && texels[0][3] == 1.f, "rt_sun the way to the sun %g %g %g", texels[0][0],
        texels[0][1], texels[0][2]);
  CHECK(texels[2][3] == 1.f && texels[4][3] == 1.f && texels[1][0] == 7.f, "rt_sun w %g, sky w %g, texel 1 %g", texels[2][3], texels[4][3], texels[1][0]);
  const auto defaults = RunSun(shared_defaults.Get(), lighting);
  CHECK(!Near(defaults[2][0], texels[2][0], 0.05f), "CONTROL the shared defaults' sun %.8g matched", defaults[2][0]);
  CHECK(!Near(defaults[4][3], texels[4][3], 0.05f), "CONTROL the shared defaults' sky w %g matched", defaults[4][3]);
  std::printf("rt_sun: sun %.6g %.6g %.6g (x %.8g), sky w %g; the shared defaults: sun x %.6g, sky w %g\n", texels[2][0], texels[2][1], texels[2][2],
              texels[2][0] / sun_colour[0], texels[4][3], defaults[2][0] / sun_colour[0], defaults[4][3]);
}

// t5's angles for the six axis normals (+x, -x, +y, -y, +z, -z): theta = x pi - pi / 2, phi = y 2 pi - pi.
constexpr std::array<std::array<float, 2>, 6> AXIS_NORMALS = {{{0.5f, 0.5f}, {0.5f, 1.f}, {1.f, 0.5f}, {0.f, 0.5f}, {0.5f, 0.75f}, {0.5f, 0.25f}}};

// A harmonics texture of 3 x 2 cells: term 0 0.5 to 1.5, the others -0.15 to 0.15 (every side's sum above 0).
static std::vector<Float4> Harmonics() {
  std::vector<Float4> texels(9 * 6);
  uint32_t state = 12345u;
  const auto next = [&state] {
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state >> 8) / 16777216.f;
  };
  for (UINT y = 0; y < 6; ++y) {
    for (UINT x = 0; x < 9; ++x) {
      const bool constant_term = (x % 3) == 0 && (y % 3) == 0;
      for (size_t channel = 0; channel < 3; ++channel) texels[(y * 9) + x][channel] = constant_term ? 0.5f + next() : (next() - 0.5f) * 0.3f;
      texels[(y * 9) + x][3] = 0.f;
    }
  }
  return texels;
}

static void TestSky(const std::string& games, const std::string& dump) {
  const auto bytes = GameBytes(dump, 0x230F2241);
  const std::string path = games + "/twwarhammer/rt_sun.cs_5_0.hlsl";
  const auto sun = ComputeShader(ReadText(path), path);
  struct Case {
    const char* name;
    bool harmonics;
    std::array<float, 4> area;  // c17: the playable area (x, z, x, z)
    float depth;                // the pixel's world z
  };
  // The pixel's world x is -0.25. Inside: (0.375, 0.3) of the area, cells (1, 0) to (2, 1); far edge: (0.99, 0.99), the
  // next cells (0, 0); outside: z 1.7 clamped and wrapped to 0.
  const std::array<Case, 4> cases = {{{"inside, between cells", true, {-1.f, 0.f, 1.f, 1.f}, 0.3f},
                                      {"far edge (the next cell wraps)", true, {-1.24f, 0.f, -0.24f, 1.f}, 0.99f},
                                      {"outside the area", true, {-1.f, 0.f, 1.f, 1.f}, 1.7f},
                                      {"harmonics off (the cube)", false, {-1.f, 0.f, 1.f, 1.f}, 0.3f}}};
  int misses = 0;
  float largest = 0.f;
  for (const auto& test : cases) {
    SunScene scene = {.lighting = SunLighting({}, {0.7f, 0.6f, 0.5f, 0.f}, 1.f), .depth = test.depth, .harmonics_width = 9, .harmonics_height = 6,
                      .harmonics = Harmonics()};
    scene.lighting.c[46] = std::bit_cast<float>(test.harmonics ? 1u : 0u);  // c11.z: g_use_spherical_harmonics
    std::copy(test.area.begin(), test.area.end(), scene.lighting.c.begin() + 68);
    const Float4 camera = {-0.25f, 3.f, test.depth, 0.f};
    const auto texels = RunSun(sun.Get(), scene.lighting, &scene, camera);
    for (size_t side = 0; side < 6; ++side) {
      scene.normal = AXIS_NORMALS[side];
      const auto game = PerAlbedo(bytes, scene);
      for (size_t channel = 0; channel < 3; ++channel) {
        const float difference = std::fabs(game[channel] - texels[3 + side][channel]);
        largest = std::max(largest, difference);
        if (difference > 1e-4f * std::fabs(game[channel]) + 2e-5f) {
          ++misses;
          std::printf("  %s side %zu channel %zu: the game's %.7g, rt_sun %.7g\n", test.name, side, channel, game[channel], texels[3 + side][channel]);
        }
      }
    }
    CHECK(texels[3][3] == 1.f && texels[6][3] == (test.harmonics ? 0.f : 1.f) && texels[4][3] == 1.f, "%s: w %g %g %g", test.name, texels[3][3],
          texels[6][3], texels[4][3]);
    if (test.harmonics) {
      const auto no_paths = RunSun(sun.Get(), scene.lighting, &scene, camera, false);
      CHECK(no_paths[3][3] == 0.f, "%s: texel 3's w %g with Path-Traced AO off", test.name, no_paths[3][3]);
    }
    if (&test == &cases[0]) {
      const auto moved = RunSun(sun.Get(), scene.lighting, &scene, {-0.25f + (2.f / 3.f), 3.f, test.depth, 0.f});
      bool differs = false;
      for (size_t side = 0; side < 6; ++side) differs = differs || !Near(moved[3 + side][0], texels[3 + side][0], 1e-3f);
      CHECK(differs, "CONTROL the camera one cell over gave the same sky");
    }
  }
  CHECK(misses == 0, "the paths' sky: %d misses", misses);
  std::printf("the paths' sky: rt_sun against 230F2241's ambient, 4 cases x 6 sides x 3 channels: %d misses (largest %.3g); CONTROL ok\n", misses,
              largest);
}

// A game's LIGHT_BOUNCE_DRAWS (its addon's table): each row's hash and ambient_final_sites (0 where unset).
static std::vector<std::pair<uint32_t, uint32_t>> SunDraws(const std::string& games, const std::string& game) {
  const std::string text = ReadText(games + "/" + game + "/addon.cpp");
  const auto start = text.find("LIGHT_BOUNCE_DRAWS = {{");
  const auto end = text.find("}};", start);
  const std::string table = text.substr(start, end - start);
  std::vector<std::pair<uint32_t, uint32_t>> draws;
  const std::regex row(R"(\{\.shader_hash = 0x([0-9A-F]{8})([^}]*)\})");
  const std::regex sites(R"(\.ambient_final_sites = (\w+))");
  for (auto found = std::sregex_iterator(table.begin(), table.end(), row); found != std::sregex_iterator(); ++found) {
    const std::string fields = (*found)[2].str();
    std::smatch value;
    uint32_t final_sites = 0;
    if (std::regex_search(fields, value, sites)) {
      const std::string name = value[1].str();
      final_sites = (name == "AMBIENT_AFTER_HARMONICS")     ? twattila::light_bounce::AMBIENT_AFTER_HARMONICS
                    : (name == "AMBIENT_AFTER_HARMONICS_Z") ? twattila::light_bounce::AMBIENT_AFTER_HARMONICS_Z
                                                            : static_cast<uint32_t>(std::stoul(name));
    }
    draws.emplace_back(static_cast<uint32_t>(std::stoul((*found)[1].str(), nullptr, 16)), final_sites);
  }
  return draws;
}

static std::vector<uint32_t> SunHashes(const std::string& games) {
  std::vector<uint32_t> hashes;
  for (const auto& [hash, final_sites] : SunDraws(games, "twwarhammer")) hashes.push_back(hash);
  return hashes;
}

static bool Multisampled(std::span<const uint8_t> code) {
  ComPtr<ID3D11ShaderReflection> reflection;
  D3DReflect(code.data(), code.size(), IID_ID3D11ShaderReflection, reinterpret_cast<void**>(reflection.GetAddressOf()));
  D3D11_SHADER_DESC desc = {};
  reflection->GetDesc(&desc);
  for (UINT index = 0; index < desc.BoundResources; ++index) {
    D3D11_SHADER_INPUT_BIND_DESC bind = {};
    reflection->GetResourceBindingDesc(index, &bind);
    if (bind.Dimension == D3D_SRV_DIMENSION_TEXTURE2DMS) return true;
  }
  return false;
}

static void TestAmbientPatch(const std::string& games, const std::string& dump) {
  const auto hashes = SunHashes(games);
  CHECK(hashes.size() == 31, "LIGHT_BOUNCE_DRAWS: %zu rows", hashes.size());
  int found = 0;
  int created = 0;
  int drawn = 0;
  int unbound_differs = 0;
  int black_differs = 0;
  int halfway_misses = 0;
  int control_moved = 0;
  for (const uint32_t hash : hashes) {
    const auto code = GameBytes(dump, hash);
    const auto shadow = ambient_patch::Patch(code, false, 1);
    bool foliage = false;
    const auto patched = ambient_patch::PatchAmbient(shadow.empty() ? std::span<const uint8_t>(code) : std::span<const uint8_t>(shadow), &foliage,
                                                     twattila::light_bounce::AMBIENT_AFTER_HARMONICS_Z);
    if (patched.empty()) {
      std::printf("  %08X: no ambient site\n", hash);
      continue;
    }
    ++found;
    ComPtr<ID3D11PixelShader> shader;
    if (FAILED(device->CreatePixelShader(patched.data(), patched.size(), nullptr, shader.GetAddressOf()))) continue;
    ++created;
    if (Multisampled(code)) continue;
    ++drawn;
    SunScene scene = {.lighting = SunLighting({1250.f, 1250.f, 1250.f, 0.f}, {0.7f, 0.6f, 0.5f, 0.f}, 1.f), .depth = 0.3f, .harmonics_width = 9,
                      .harmonics_height = 6, .harmonics = Harmonics()};
    scene.lighting.c[46] = std::bit_cast<float>(1u);
    const std::array<float, 4> area = {-1.f, 0.f, 1.f, 1.f};
    std::copy(area.begin(), area.end(), scene.lighting.c.begin() + 68);
    const auto game = DrawGame(code, scene);
    const auto unbound = DrawGame(patched, scene);
    SunScene black = scene;
    std::fill(black.harmonics.begin(), black.harmonics.end(), Float4{});
    std::fill(black.lighting.c.begin() + 8, black.lighting.c.begin() + 32, 0.f);
    const auto game_black = DrawGame(code, black);
    scene.ambient_off = 1.f;
    const auto off = DrawGame(patched, scene);
    scene.ambient_off = 0.5f;
    const auto half = DrawGame(patched, scene);
    bool moved = false;
    for (size_t channel = 0; channel < 3; ++channel) {
      unbound_differs += (std::bit_cast<uint32_t>(unbound[channel]) != std::bit_cast<uint32_t>(game[channel])) ? 1 : 0;
      black_differs += Near(off[channel], game_black[channel], 1e-5f) ? 0 : 1;
      halfway_misses += Near(half[channel], 0.5f * (game[channel] + off[channel]), 1e-4f) ? 0 : 1;
      moved = moved || !Near(off[channel], unbound[channel], 1e-3f);
    }
    control_moved += moved ? 1 : 0;
  }
  CHECK(found == 31 && created == 31, "the ambient patch: found %d, created %d of 31", found, created);
  CHECK(drawn == 11 && unbound_differs == 0 && black_differs == 0 && halfway_misses == 0, "drawn %d: t51 unbound %d differ, t51 = 1 %d differ from black, halfway %d",
        drawn, unbound_differs, black_differs, halfway_misses);
  CHECK(control_moved == drawn, "CONTROL t51 = 1 moved %d of %d", control_moved, drawn);
  std::printf("the ambient patch (AMBIENT_AFTER_HARMONICS_Z): found %d, created %d of 31; %d drawn: t51 unbound %d differ, t51 = 1 %d differ from the "
              "black ambient, halfway %d misses; CONTROL %d of %d moved\n",
              found, created, drawn, unbound_differs, black_differs, halfway_misses, control_moved, drawn);
}

static void TestSharedBytes(const std::string& games, const std::string& dump) {
  const std::string pack = "dump_shader_twwarhammer_pack";
  const auto at = dump.find(pack);
  CHECK(at != std::string::npos, "the dump folder %s is not %s's", dump.c_str(), pack.c_str());
  if (at == std::string::npos) return;
  int other_rows = 0;
  int other_patched = 0;
  int other_differ = 0;
  int ours_differ = 0;
  int all_shaders = 0;
  int defaults_differ = 0;
  for (const char* game : {"twattila", "twpharaohdynasties", "twrome2", "twthronesofbritannia", "twtroy", "twwarhammer2", "twwarhammer3", "twwarhammer"}) {
    const std::string game_dump = std::string(dump).replace(at, pack.size(), std::string("dump_shader_") + game + "_pack");
    int rows = 0;
    int patched = 0;
    int differ = 0;
    for (const auto& [hash, final_sites] : SunDraws(games, game)) {
      const auto code = GameBytes(game_dump, hash);
      if (code.empty()) continue;
      ++rows;
      const auto now_shadow = ambient_patch::Patch(code, false, 1);
      const auto before_shadow = ambient_patch_before::Patch(code, false, 1);
      bool foliage = false;
      const auto now = ambient_patch::PatchAmbient(now_shadow.empty() ? std::span<const uint8_t>(code) : std::span<const uint8_t>(now_shadow), &foliage,
                                                   final_sites);
      const auto before = ambient_patch_before::PatchAmbient(
          before_shadow.empty() ? std::span<const uint8_t>(code) : std::span<const uint8_t>(before_shadow), &foliage, final_sites);
      patched += now.empty() ? 0 : 1;
      differ += (now != before || now_shadow != before_shadow) ? 1 : 0;
    }
    // Patch with its defaults on every PS in its dump, forward and not.
    int shaders = 0;
    int defaults_patched = 0;
    for (const auto& entry : std::filesystem::directory_iterator(game_dump)) {
      if (entry.path().filename().string().find(".ps_") == std::string::npos) continue;
      std::ifstream file(entry.path(), std::ios::binary);
      const std::vector<uint8_t> code{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
      ++shaders;
      for (const bool forward : {false, true}) {
        const auto now = ambient_patch::Patch(code, forward, 1);
        defaults_patched += now.empty() ? 0 : 1;
        defaults_differ += (now != ambient_patch_before::Patch(code, forward, 1)) ? 1 : 0;
      }
    }
    all_shaders += shaders;
    std::printf("  %s: %d rows in its dump, %d patched, %d differ from before; Patch's defaults on its %d PS: %d patched\n", game, rows, patched,
                differ, shaders, defaults_patched);
    if (std::string(game) == "twwarhammer") {
      ours_differ = differ;
    } else {
      other_rows += rows;
      other_patched += patched;
      other_differ += differ;
    }
  }
  CHECK(other_rows > 100 && other_differ == 0, "the other games: %d rows, %d differ", other_rows, other_differ);
  CHECK(ours_differ == 31, "CONTROL Warhammer's: %d of 31 differ", ours_differ);
  CHECK(all_shaders > 10000 && defaults_differ == 0, "Patch's defaults: %d of %d PS (x2) differ", defaults_differ, all_shaders);
  int declared = 0;
  for (const uint32_t hash : {0x0BF2DB7Fu, 0xBCB9B1D6u}) {
    const auto code = GameBytes(dump, hash);
    declared += (ambient_patch_before::Patch(code, true, 1).empty() && !ambient_patch::Patch(code, true, 1, true).empty()) ? 1 : 0;
  }
  CHECK(declared == 2, "CONTROL declare_position: %d of 2 patch only with it", declared);
  std::printf("shared Patch: its defaults on all %d PS of the 8 dumps (forward and not) %d differ from before; CONTROL declare_position "
              "patches %d of 0BF2DB7F / BCB9B1D6 (before: not)\n",
              all_shaders, defaults_differ, declared);
  std::printf("shared PatchAmbient: the other games' %d rows (%d patched) the same bytes as before; CONTROL Warhammer's %d of 31 differ\n", other_rows,
              other_patched, ours_differ);
}

// The resolve's inputs at `size` x `size` (`view` x `view` of them the view's: the same texels; the rest 7s, the radiance's
// ramp running on): normal (theta 0, phi -3 pi / 8: the reflection away from the camera), roughness 0.5, specular colour 0.5 with the SSR flag, depth 0.1 (10 m),
// the march's hit (confidence 1, `hit`), the scene's radiance a ramp (x + 0.5, y + 0.5, 1) + `radiance`; the rays' reflections at
// t47 (`rays` 0.3, 0.2, 0.1 and confidence 0.8) or unbound.
struct ResolveRun {
  UINT size = 64;
  float share = 0.f;  // cb12's frame share (0: unbound)
  float hit = 65504.f;
  float radiance = 0.f;
  bool rays = false;
};

static std::vector<Float4> RunResolve(ID3D11ComputeShader* shader, const ResolveRun& run) {
  constexpr UINT VIEW = 64;
  const UINT size = run.size;
  std::vector<Float4> normal(size * size, Float4{7.f, 7.f, 7.f, 7.f});
  std::vector<Float4> specular = normal;
  std::vector<Float4> depth = normal;
  std::vector<Float4> radiance(size * size);
  for (UINT y = 0; y < size; ++y) {
    for (UINT x = 0; x < size; ++x) {
      const size_t at = (static_cast<size_t>(y) * size) + x;
      radiance[at] = {static_cast<float>(x) + 0.5f + run.radiance, static_cast<float>(y) + 0.5f + run.radiance, 1.f + run.radiance, 1.f};
      if (x >= VIEW || y >= VIEW) continue;
      normal[at] = {0.5f, 0.3125f, 0.5f, 0.f};
      specular[at] = {0.5f, 0.5f, 0.5f, 1.f / 255.f};
      depth[at] = {0.1f, 0.f, 0.f, 0.f};
    }
  }
  const std::vector<Float4> cube(6, Float4{0.25f, 0.25f, 0.25f, 1.f});
  const auto cube_texture = Texture(1, 1, cube.data(), D3D11_BIND_SHADER_RESOURCE, 6, D3D11_RESOURCE_MISC_TEXTURECUBE);
  ComPtr<ID3D11ShaderResourceView> cube_view;
  const D3D11_SHADER_RESOURCE_VIEW_DESC cube_desc = {.Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY,
                                                     .TextureCubeArray = {.MostDetailedMip = 0, .MipLevels = 1, .First2DArrayFace = 0, .NumCubes = 1}};
  device->CreateShaderResourceView(cube_texture.Get(), &cube_desc, cube_view.GetAddressOf());
  const std::array<ID3D11ShaderResourceView*, 7> views = {
      cube_view.Get(), View(size, size, normal).Detach(), View(size, size, specular).Detach(), View(size, size, depth).Detach(),
      View(64, 64, std::vector<Float4>(64 * 64, Float4{1.f, 0.5f, 0.5f, 0.f})).Detach(),
      View(size / 2, size / 2, std::vector<Float4>((size / 2) * (size / 2), Float4{1.f, run.hit, 0.f, 0.f})).Detach(), View(size, size, radiance).Detach()};
  const auto output = Texture(size, size, nullptr, D3D11_BIND_UNORDERED_ACCESS);
  ComPtr<ID3D11UnorderedAccessView> uav;
  device->CreateUnorderedAccessView(output.Get(), nullptr, uav.GetAddressOf());
  std::array<float, 16 * 4> environment = {};
  environment[0] = 1.f;       // weight 0
  environment[15 * 4 + 1] = 1.f;  // g_environment_count
  std::array<float, 39 * 4> camera = {};
  for (const size_t row : {0u, 1u, 2u}) camera[(1 + row) * 4 + row] = 1.f;  // view: identity (the camera at the origin)
  camera[4 * 4 + 3] = 1.f;
  camera[5 * 4 + 0] = 1.f;  // projection: x, y over z
  camera[6 * 4 + 1] = 1.f;
  camera[8 * 4 + 2] = 1.f;
  camera[21 * 4 + 0] = 1.f;  // inverse view-projection: (x, y, 1) over depth
  camera[22 * 4 + 1] = 1.f;
  camera[23 * 4 + 3] = 1.f;
  camera[24 * 4 + 2] = 1.f;
  camera[25 * 4 + 0] = 9.5f;  // near, far: around the surface (10 m), the march's end on screen near its pixel
  camera[25 * 4 + 1] = 10.5f;
  const auto extent = static_cast<float>(size);
  camera[27 * 4 + 0] = extent;  // g_screen_size: the targets'
  camera[27 * 4 + 1] = extent;
  camera[27 * 4 + 2] = 1.f / extent;
  camera[27 * 4 + 3] = 1.f / extent;
  const std::array<float, 4> render_scale = {1.f, 1.f, run.share, run.share};
  const std::array<ID3D11Buffer*, 2> buffers = {Constants(environment.data(), 256).Detach(), Constants(camera.data(), 39 * 16).Detach()};
  const auto scale = Constants(render_scale.data(), 16);
  const D3D11_SAMPLER_DESC point = {.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
                                    .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP, .MaxLOD = D3D11_FLOAT32_MAX};
  D3D11_SAMPLER_DESC linear = point;
  linear.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  ComPtr<ID3D11SamplerState> point_sampler;
  ComPtr<ID3D11SamplerState> linear_sampler;
  device->CreateSamplerState(&point, point_sampler.GetAddressOf());
  device->CreateSamplerState(&linear, linear_sampler.GetAddressOf());
  const std::array<ID3D11SamplerState*, 5> samplers = {linear_sampler.Get(), point_sampler.Get(), point_sampler.Get(), point_sampler.Get(), linear_sampler.Get()};
  context->CSSetShader(shader, nullptr, 0);
  context->CSSetConstantBuffers(0, 2, buffers.data());
  if (run.share > 0.f) context->CSSetConstantBuffers(12, 1, scale.GetAddressOf());
  context->CSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
  const auto rays = View(size, size, std::vector<Float4>(static_cast<size_t>(size) * size, Float4{0.3f, 0.2f, 0.1f, 0.8f}));
  if (run.rays) context->CSSetShaderResources(47, 1, rays.GetAddressOf());
  context->CSSetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());
  context->CSSetUnorderedAccessViews(0, 1, uav.GetAddressOf(), nullptr);
  context->Dispatch((size + 31) / 32, (size + 31) / 32, 1);
  context->ClearState();
  for (auto* buffer : buffers) buffer->Release();
  for (size_t index = 1; index < views.size(); ++index) views[index]->Release();
  return Read(output.Get());
}

// Where the march's read landed (texels from the pixel's centre): the output is (the read - the cube) x F with the read's z 1.
static std::array<float, 2> Offset(const Float4& texel, UINT x, UINT y) {
  const float f = texel[2] / 0.75f;
  return {(texel[0] / f) + 0.25f - (static_cast<float>(x) + 0.5f), (texel[1] / f) + 0.25f - (static_cast<float>(y) + 0.5f)};
}

static void TestResolve(const std::string& games) {
  const std::string path = games + "/twwarhammer/shaders/ssr00_0x6B80E35B.cs_5_0.hlsl";
  const std::string source = ReadText(path);
  const auto fixed = ComputeShader(source, path);
  const auto no_position = ComputeShader(Without(source, "(r0.zw / FrameRenderShare())", "r0.zw"), path);
  const auto no_projected = ComputeShader(Without(source, "(r2.xz * float2(0.5,-0.5) + float2(0.5,0.5)) * FrameRenderShare()",
                                                  "(r2.xz * float2(0.5,-0.5) + float2(0.5,0.5))"),
                                          path);
  constexpr UINT VIEW = 64;
  constexpr float MARCH = 64000.f;  // -log2(64000 x 1.52662433e-005): 0.0335 of the uv
  const auto full = RunResolve(fixed.Get(), {.size = VIEW});
  const auto half = RunResolve(fixed.Get(), {.size = VIEW * 2, .share = 0.5f});
  const auto unbound = RunResolve(fixed.Get(), {.size = VIEW * 2});
  const auto half_no_position = RunResolve(no_position.Get(), {.size = VIEW * 2, .share = 0.5f});
  const auto full_march = RunResolve(fixed.Get(), {.size = VIEW, .hit = MARCH});
  const auto half_march = RunResolve(fixed.Get(), {.size = VIEW * 2, .share = 0.5f, .hit = MARCH});
  const auto half_march_no_projected = RunResolve(no_projected.Get(), {.size = VIEW * 2, .share = 0.5f, .hit = MARCH});
  int same = 0;
  int unbound_differs = 0;
  int position_differs = 0;
  int directions = 0;
  int projected_differs = 0;
  float worst_cos = 1.f;
  float worst_ratio = 0.f;
  float output_sum = 0.f;
  for (UINT y = 0; y < VIEW; ++y) {
    for (UINT x = 0; x < VIEW; ++x) {
      const auto& a = full[(y * VIEW) + x];
      const auto& b = half[(y * VIEW * 2) + x];
      const auto& c = unbound[(y * VIEW * 2) + x];
      const auto& d = half_no_position[(y * VIEW * 2) + x];
      bool equal = true;
      bool c_differs = false;
      bool d_differs = false;
      for (size_t channel = 0; channel < 3; ++channel) {
        equal = equal && Near(a[channel], b[channel], 1e-4f);
        c_differs = c_differs || !Near(a[channel], c[channel], 1e-3f);
        d_differs = d_differs || !Near(a[channel], d[channel], 1e-3f);
        output_sum += std::fabs(a[channel]);
      }
      same += equal ? 1 : 0;
      unbound_differs += c_differs ? 1 : 0;
      position_differs += d_differs ? 1 : 0;
      if (x < 8 || y < 8 || x >= VIEW - 8 || y >= VIEW - 8) continue;
      const auto o_full = Offset(full_march[(y * VIEW) + x], x, y);
      const auto o_half = Offset(half_march[(y * VIEW * 2) + x], x, y);
      const auto o_none = Offset(half_march_no_projected[(y * VIEW * 2) + x], x, y);
      const float l_full = std::hypot(o_full[0], o_full[1]);
      const float l_half = std::hypot(o_half[0], o_half[1]);
      const float l_none = std::hypot(o_none[0], o_none[1]);
      if (l_full < 0.5f) continue;
      ++directions;
      const float cos_half = ((o_full[0] * o_half[0]) + (o_full[1] * o_half[1])) / (l_full * l_half);
      const float cos_none = ((o_full[0] * o_none[0]) + (o_full[1] * o_none[1])) / (l_full * l_none);
      worst_cos = std::min(worst_cos, cos_half);
      worst_ratio = std::max(worst_ratio, std::fabs((l_half / l_full) - 2.f));
      projected_differs += (cos_none < 0.999f) ? 1 : 0;
    }
  }
  constexpr int PIXELS = VIEW * VIEW;
  CHECK(output_sum > 1.f, "the resolve wrote nothing (%g)", output_sum);
  CHECK(same == PIXELS, "50%% vs 100%%: %d of %d the same", same, PIXELS);
  CHECK(directions > 1000, "the march's offsets: %d pixels", directions);
  CHECK(worst_cos > 0.9999f, "the march's direction at 50%%: worst cos %.6f", worst_cos);
  CHECK(worst_ratio < 0.01f, "the march's length at 50%%: 2 x in texels (uv the same) off by %.4f", worst_ratio);
  CHECK(unbound_differs > PIXELS / 2, "CONTROL cb12 unbound at 50%%: %d of %d differ", unbound_differs, PIXELS);
  CHECK(position_differs > PIXELS / 2, "CONTROL without the position's share: %d of %d differ", position_differs, PIXELS);
  CHECK(projected_differs > directions / 2, "CONTROL without the projected uv's share: %d of %d directions differ", projected_differs, directions);
  std::printf("SSR resolve: 50%% vs 100%% %d of %d the same; the march's direction %d pixels, worst cos %.6f, length 2 x off by %.4f; CONTROLS "
              "unbound %d, no position share %d, no projected share %d of %d\n",
              same, PIXELS, directions, worst_cos, worst_ratio, unbound_differs, position_differs, projected_differs, directions);
}

// One output element to stream out: its semantic, index and component count.
struct Streamed {
  const char* semantic;
  UINT index;
  BYTE components;
};

// `code`'s outputs streamed out for `vertices` (floats, one point each) laid out by the game vertex shader's input signature,
// as rt_core draws a capture with the game's layout; `constants` from b0, t0 bound.
static std::vector<float> StreamOut(std::span<const uint8_t> game, std::span<const uint8_t> code, std::span<const Streamed> outputs,
                                    const std::vector<float>& vertices, std::span<ID3D11Buffer* const> constants, ID3D11ShaderResourceView* t0 = nullptr) {
  ComPtr<ID3D11ShaderReflection> reflection;
  D3DReflect(game.data(), game.size(), IID_ID3D11ShaderReflection, reinterpret_cast<void**>(reflection.GetAddressOf()));
  D3D11_SHADER_DESC desc = {};
  reflection->GetDesc(&desc);
  constexpr std::array<DXGI_FORMAT, 4> FORMATS = {DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32B32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT};
  std::vector<D3D11_INPUT_ELEMENT_DESC> layout;
  UINT vertex_bytes = 0;
  for (UINT index = 0; index < desc.InputParameters; ++index) {
    D3D11_SIGNATURE_PARAMETER_DESC parameter = {};
    reflection->GetInputParameterDesc(index, &parameter);
    const auto components = static_cast<UINT>(std::bit_width(static_cast<UINT>(parameter.Mask)));
    layout.push_back({parameter.SemanticName, parameter.SemanticIndex, FORMATS[components - 1], 0, vertex_bytes, D3D11_INPUT_PER_VERTEX_DATA, 0});
    vertex_bytes += components * 4;
  }
  std::vector<D3D11_SO_DECLARATION_ENTRY> entries;
  UINT stride = 0;
  for (const auto& output : outputs) {
    entries.push_back({0, output.semantic, output.index, 0, output.components, 0});
    stride += output.components * 4u;
  }
  const auto count = static_cast<UINT>(vertices.size() * 4 / vertex_bytes);
  ComPtr<ID3D11InputLayout> input_layout;
  ComPtr<ID3D11VertexShader> vertex_shader;
  ComPtr<ID3D11GeometryShader> stream;
  if (FAILED(device->CreateInputLayout(layout.data(), static_cast<UINT>(layout.size()), game.data(), game.size(), input_layout.GetAddressOf()))
      || FAILED(device->CreateVertexShader(code.data(), code.size(), nullptr, vertex_shader.GetAddressOf()))
      || FAILED(device->CreateGeometryShaderWithStreamOutput(code.data(), code.size(), entries.data(), static_cast<UINT>(entries.size()), &stride, 1,
                                                             D3D11_SO_NO_RASTERIZED_STREAM, nullptr, stream.GetAddressOf()))) {
    return {};
  }
  const D3D11_BUFFER_DESC vertex_desc = {.ByteWidth = static_cast<UINT>(vertices.size() * 4), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_VERTEX_BUFFER};
  const D3D11_SUBRESOURCE_DATA vertex_data = {.pSysMem = vertices.data()};
  ComPtr<ID3D11Buffer> vertex_buffer;
  device->CreateBuffer(&vertex_desc, &vertex_data, vertex_buffer.GetAddressOf());
  D3D11_BUFFER_DESC out_desc = {.ByteWidth = count * stride, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_STREAM_OUTPUT};
  ComPtr<ID3D11Buffer> out;
  device->CreateBuffer(&out_desc, nullptr, out.GetAddressOf());
  const UINT zero = 0;
  context->IASetVertexBuffers(0, 1, vertex_buffer.GetAddressOf(), &vertex_bytes, &zero);
  context->IASetInputLayout(input_layout.Get());
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
  context->VSSetShader(vertex_shader.Get(), nullptr, 0);
  context->VSSetConstantBuffers(0, static_cast<UINT>(constants.size()), constants.data());
  context->VSSetShaderResources(0, 1, &t0);
  context->GSSetShader(stream.Get(), nullptr, 0);
  context->SOSetTargets(1, out.GetAddressOf(), &zero);
  context->Draw(count, 0);
  context->ClearState();
  out_desc = {.ByteWidth = count * stride, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
  ComPtr<ID3D11Buffer> staging;
  device->CreateBuffer(&out_desc, nullptr, staging.GetAddressOf());
  context->CopyResource(staging.Get(), out.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  std::vector<float> values(count * stride / 4);
  std::memcpy(values.data(), mapped.pData, values.size() * 4);
  context->Unmap(staging.Get(), 0);
  return values;
}

static std::vector<uint8_t> Bytes(const std::string& path) {
  const std::string text = ReadText(path);
  return {text.begin(), text.end()};
}

// rt_capture.hlsli's RtHalfBits back to a float (its tiny values are 0).
static float HalfValue(uint32_t half) {
  const int exponent = static_cast<int>((half >> 10) & 0x1F);
  const float magnitude = (exponent == 0) ? 0.f : std::ldexp(1.f + (static_cast<float>(half & 0x3FF) / 1024.f), exponent - 15);
  return ((half & 0x8000) != 0) ? -magnitude : magnitude;
}

static void TestCasters(const std::string& root, const std::string& dump) {
  const std::string embed = root + "/build/twwarhammer.include/embed/";
  uint32_t state = 4242u;
  const auto unit = [&state](float low, float high) {
    state = state * 1664525u + 1013904223u;
    return low + ((high - low) * static_cast<float>(state >> 8) / 16777216.f);
  };
  // The far trees' cards (CA6BCF19: POSITION0 4, TEXCOORD0 2, TEXCOORD1 4, TEXCOORD2 4, TEXCOORD3 1).
  const auto card = GameBytes(dump, 0xCA6BCF19);
  const auto card_capture = Bytes(embed + "rt_capture_far_tree.cso");
  int card_values = 0;
  int card_misses = 0;
  int card_control = 0;
  for (int run = 0; run < 16; ++run) {
    const std::array<float, 3> place = {unit(-500.f, 500.f), unit(0.f, 50.f), unit(-500.f, 500.f)};
    const std::array<float, 2> facing = {unit(-1.f, 1.f), unit(-1.f, 1.f)};
    const float scale = unit(5.f, 30.f);
    const float sun_turn = unit(0.f, 6.2831853f);
    const float sun_up = unit(0.1f, 0.9f);
    const float across = std::sqrt(1.f - (sun_up * sun_up));
    const Float4 toward_sun = {across * std::sin(sun_turn), sun_up, across * std::cos(sun_turn), 1.f};
    std::vector<float> vertices;
    for (int corner = 0; corner < 8; ++corner) {
      vertices.insert(vertices.end(), {unit(-0.5f, 0.5f), unit(0.f, 1.f), 0.f, 1.f, unit(0.f, 0.25f), unit(0.f, 1.f), place[0], place[1], place[2], 1.f,
                                       facing[0], 0.f, facing[1], scale, 0.f});
    }
    const auto sun = View(1, 1, {toward_sun});
    for (const bool control : {false, true}) {
      // The camera along the sun's way across (CONTROL: 90 degrees off), any height; view_projection identity.
      std::array<float, 48 * 4> camera = {};
      const std::array<float, 2> way = control ? std::array<float, 2>{-toward_sun[2] / across, toward_sun[0] / across}
                                               : std::array<float, 2>{toward_sun[0] / across, toward_sun[2] / across};
      camera[0] = place[0] + (way[0] * 200.f);
      camera[1] = place[1] + unit(-50.f, 300.f);
      camera[2] = place[2] + (way[1] * 200.f);
      for (size_t row = 0; row < 4; ++row) camera[(9 + row) * 4 + row] = 1.f;
      const auto buffer = Constants(camera.data(), sizeof(camera));
      const std::array<ID3D11Buffer*, 1> constants = {buffer.Get()};
      const std::array<Streamed, 1> game_world = {{{"TEXCOORD", 2, 3}}};
      const std::array<Streamed, 1> capture_world = {{{"TEXCOORD", 0, 3}}};
      const auto game = StreamOut(card, card, game_world, vertices, constants);
      const auto ours = StreamOut(card, card_capture, capture_world, vertices, constants, sun.Get());
      CHECK(!game.empty() && game.size() == ours.size(), "the far cards' stream out");
      for (size_t at = 0; at < std::min(game.size(), ours.size()); ++at) {
        const bool miss = std::fabs(game[at] - ours[at]) > 1e-3f;
        if (control) {
          card_control += miss ? 1 : 0;
        } else {
          ++card_values;
          card_misses += miss ? 1 : 0;
        }
      }
    }
  }
  CHECK(card_values == 16 * 8 * 3 && card_misses == 0 && card_control > 0, "far cards: %d values, %d misses, CONTROL %d differ", card_values, card_misses,
        card_control);
  std::printf("far cards: rt_capture_far_tree against CA6BCF19, %d values, %d misses; CONTROL camera 90 degrees off: %d differ\n", card_values,
              card_misses, card_control);

  // The campaign plants (84797031: POSITION0 4 with the UV's u, NORMAL0 4 with its v, COLOR0 4 (w the wind's weight), POSITION1
  // 4 (the place, w the turn), POSITION2 1 (the size)); no wind (wind_VS 0).
  const auto plant = GameBytes(dump, 0x84797031);
  const auto plant_capture = Bytes(embed + "rt_capture_campaign_vegetation.cso");
  const auto plant_rows = Bytes(embed + "rt_rows_campaign_vegetation.cso");
  int plant_values = 0;
  int plant_misses = 0;
  int uv_misses = 0;
  int row_misses = 0;
  int plant_control = 0;
  const std::array<float, 2 * 4> wind = {};
  const auto wind_buffer = Constants(wind.data(), sizeof(wind));
  for (int run = 0; run < 16; ++run) {
    const std::array<float, 5> instance = {unit(-500.f, 500.f), unit(0.f, 50.f), unit(-500.f, 500.f), unit(-6.f, 6.f), unit(0.2f, 3.f)};
    std::vector<float> vertices;
    for (int vertex = 0; vertex < 8; ++vertex) {
      vertices.insert(vertices.end(), {unit(-2.f, 2.f), unit(0.f, 6.f), unit(-2.f, 2.f), unit(0.f, 1.f), 0.f, 1.f, 0.f, unit(0.f, 1.f), 0.f, 0.f, 0.f,
                                       unit(0.f, 1.f), instance[0], instance[1], instance[2], instance[3], instance[4]});
    }
    for (const bool control : {false, true}) {
      // The camera 80 m across, below the plants (CONTROL: 100 m above: the game leans them); view_projection identity.
      std::array<float, 27 * 4> camera = {};
      camera[0] = instance[0] + 80.f;
      camera[1] = instance[1] + (control ? 100.f : -5.f);
      camera[2] = instance[2];
      for (size_t row = 0; row < 4; ++row) camera[(9 + row) * 4 + row] = 1.f;
      camera[26 * 4] = unit(0.f, 100.f);
      const auto buffer = Constants(camera.data(), sizeof(camera));
      const std::array<ID3D11Buffer*, 2> constants = {buffer.Get(), wind_buffer.Get()};
      const std::array<Streamed, 1> world_uv = {{{"TEXCOORD", 0, 4}}};
      const std::array<Streamed, 3> rows = {{{"TEXCOORD", 0, 4}, {"TEXCOORD", 1, 4}, {"TEXCOORD", 2, 4}}};
      const auto game = StreamOut(plant, plant, world_uv, vertices, constants);
      const auto ours = StreamOut(plant, plant_capture, world_uv, vertices, constants);
      const auto placed = StreamOut(plant, plant_rows, rows, vertices, constants);
      CHECK(game.size() == 8 * 4 && ours.size() == 8 * 4 && placed.size() == 8 * 12, "the campaign plants' stream out");
      if (game.size() != 8 * 4 || ours.size() != 8 * 4 || placed.size() != 8 * 12) return;
      for (size_t vertex = 0; vertex < 8; ++vertex) {
        const float* local = vertices.data() + (vertex * 17);
        for (size_t axis = 0; axis < 3; ++axis) {
          const float expected = game[(vertex * 4) + axis];
          const bool miss = std::fabs(expected - ours[(vertex * 4) + axis]) > 1e-3f;
          if (control) {
            plant_control += miss ? 1 : 0;
            continue;
          }
          ++plant_values;
          plant_misses += miss ? 1 : 0;
          const float* row = placed.data() + (vertex * 12) + (axis * 4);
          const float moved = (row[0] * local[0]) + (row[1] * local[1]) + (row[2] * local[2]) + row[3];
          row_misses += (std::fabs(expected - moved) > 1e-3f) ? 1 : 0;
        }
        if (!control) {
          const uint32_t halves = std::bit_cast<uint32_t>(ours[(vertex * 4) + 3]);
          uv_misses += Near(HalfValue(halves & 0xFFFF), local[3], 2e-3f) ? 0 : 1;
          uv_misses += Near(HalfValue(halves >> 16), local[7], 2e-3f) ? 0 : 1;
        }
      }
    }
  }
  CHECK(plant_values == 16 * 8 * 3 && plant_misses == 0 && uv_misses == 0 && row_misses == 0 && plant_control > 0,
        "campaign plants: %d values, %d misses, %d UV, %d rows, CONTROL %d differ", plant_values, plant_misses, uv_misses, row_misses, plant_control);
  std::printf("campaign plants: rt_capture_campaign_vegetation against 84797031, %d values, %d misses, UV %d misses; rows %d misses; CONTROL "
              "camera above: %d differ\n",
              plant_values, plant_misses, uv_misses, row_misses, plant_control);
}

// The point light shaders' inputs (VS B5B5BFA4's outputs): a full-screen triangle with the light's colour (POSITION1) and
// place and reach (POSITION2) from b0.
static ComPtr<ID3D11VertexShader> light_vertex;

// A point light shader's bytes drawn over a 4 x 4 G-buffer (each pixel its own normal, albedo, specular and depth; the
// camera's inverse view-projection identity), its constants bound by name as the game's bytes (`game`: the build strips
// ours of reflection) name them (g_hdr_on 1, gamma 1, the viewport offset 0 / scale 1); nothing at t42 / b9.
static std::vector<Float4> DrawLight(std::span<const uint8_t> code, std::span<const uint8_t> game, const std::array<float, 8>& light, uint32_t seed) {
  ComPtr<ID3D11PixelShader> shader;
  if (FAILED(device->CreatePixelShader(code.data(), code.size(), nullptr, shader.GetAddressOf()))) return {};
  ComPtr<ID3D11ShaderReflection> reflection;
  if (FAILED(D3DReflect(game.data(), game.size(), IID_ID3D11ShaderReflection, reinterpret_cast<void**>(reflection.GetAddressOf())))) return {};
  std::array<float, 40 * 4> camera = {};
  for (size_t row = 0; row < 4; ++row) camera[(21 + row) * 4 + row] = 1.f;
  camera[2] = -3.f;  // the camera behind
  camera[27 * 4 + 0] = 4.f;
  camera[27 * 4 + 1] = 4.f;
  camera[27 * 4 + 2] = 0.25f;
  camera[27 * 4 + 3] = 0.25f;
  const std::array<float, 4> colorimetry = {0.f, 1.f, 1.f, 0.f};
  std::array<float, 18 * 4> lighting = {};
  lighting[44] = 1.f;  // g_hdr_on
  const std::array<float, 4> viewport = {0.f, 0.f, 1.f, 1.f};
  std::vector<Float4> gbuffer[4];
  for (UINT pixel = 0; pixel < 16; ++pixel) {
    seed = seed * 1664525u + 1013904223u;
    const auto next = [&seed] {
      seed = seed * 1664525u + 1013904223u;
      return static_cast<float>(seed >> 8) / 16777216.f;
    };
    gbuffer[0].push_back({next(), next(), 0.f, 0.f});
    gbuffer[1].push_back({next(), next(), next(), 0.f});
    gbuffer[2].push_back({next() * 0.2f, next() * 0.2f, next() * 0.2f, static_cast<float>(pixel % 4) / 255.f});
    gbuffer[3].push_back({0.2f + (next() * 0.6f), 0.f, 0.f, 0.f});
  }
  std::vector<ComPtr<ID3D11Buffer>> buffers;
  std::vector<ComPtr<ID3D11ShaderResourceView>> views;
  D3D11_SHADER_DESC desc = {};
  reflection->GetDesc(&desc);
  for (UINT index = 0; index < desc.BoundResources; ++index) {
    D3D11_SHADER_INPUT_BIND_DESC bind = {};
    reflection->GetResourceBindingDesc(index, &bind);
    const std::string name = bind.Name;
    if (bind.Type == D3D_SIT_CBUFFER) {
      D3D11_SHADER_BUFFER_DESC buffer_desc = {};
      reflection->GetConstantBufferByName(bind.Name)->GetDesc(&buffer_desc);
      std::vector<uint8_t> data((buffer_desc.Size + 15) & ~15u);
      const auto fill = [&](const void* source, size_t bytes) { std::memcpy(data.data(), source, std::min(bytes, data.size())); };
      if (name == "camera") fill(camera.data(), sizeof(camera));
      if (name == "colorimetry_VS_PS") fill(colorimetry.data(), sizeof(colorimetry));
      if (name == "lighting_VS_PS") fill(lighting.data(), sizeof(lighting));
      if (name == "dynamic_light_PS") fill(viewport.data(), sizeof(viewport));
      buffers.push_back(Constants(data.data(), static_cast<UINT>(data.size())));
      context->PSSetConstantBuffers(bind.BindPoint, 1, buffers.back().GetAddressOf());
    } else if (bind.Type == D3D_SIT_TEXTURE && name.starts_with("gbuffer_channel_")) {
      views.push_back(View(4, 4, gbuffer[name[16] - '1']));
      context->PSSetShaderResources(bind.BindPoint, 1, views.back().GetAddressOf());
    }
  }
  const auto light_constants = Constants(light.data(), sizeof(light));
  const auto target = Texture(4, 4, nullptr, D3D11_BIND_RENDER_TARGET);
  ComPtr<ID3D11RenderTargetView> rtv;
  device->CreateRenderTargetView(target.Get(), nullptr, rtv.GetAddressOf());
  context->VSSetShader(light_vertex.Get(), nullptr, 0);
  context->VSSetConstantBuffers(0, 1, light_constants.GetAddressOf());
  context->PSSetShader(shader.Get(), nullptr, 0);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D11_VIEWPORT viewport_4 = {.Width = 4.f, .Height = 4.f, .MaxDepth = 1.f};
  context->RSSetViewports(1, &viewport_4);
  context->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
  context->Draw(3, 0);
  context->ClearState();
  return Read(target.Get());
}

static void TestPointLights(const std::string& root, const std::string& dump) {
  const auto vertex = CompileText("cbuffer light : register(b0) { float4 colour; float4 place; };"
                                  "void main(uint id : SV_VertexID, out float4 position : SV_Position, out float4 uv : TEXCOORD0,"
                                  " out float4 light_colour : POSITION1, out float4 light_place : POSITION2) {"
                                  " const float2 corner = float2((id << 1) & 2, id & 2); position = float4(corner * float2(2, -2) + float2(-1, 1), 0, 1);"
                                  " uv = 0; light_colour = colour; light_place = place; }",
                                  "light", "main", "vs_5_0");
  device->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(), nullptr, light_vertex.GetAddressOf());
  const std::string embed = root + "/build/twwarhammer.include/embed/";
  int compared = 0;
  int misses = 0;
  int lit = 0;
  int control = 0;
  float largest = 0.f;
  for (const uint32_t hash : {0xD0C67938u, 0x5D1695BCu}) {
    const auto game = GameBytes(dump, hash);
    char name[32];
    std::snprintf(name, sizeof(name), "0x%08X.cso", hash);
    const auto ours = Bytes(embed + name);
    const auto other = GameBytes(dump, hash == 0xD0C67938u ? 0x5D1695BCu : 0xD0C67938u);
    for (uint32_t run = 0; run < 8; ++run) {
      // A light 1-3 m from the pixels (their world (-0.75..0.75, -0.75..0.75, 0.2..0.8)), reach 2-6 m, colour as ATN's torches.
      const float turn = static_cast<float>(run) * 0.8f;
      const std::array<float, 8> light = {15000.f, 5764.7f, 1529.4f, 1.f, std::sin(turn), std::cos(turn), -0.5f - (0.25f * static_cast<float>(run % 3)),
                                          2.f + (0.5f * static_cast<float>(run))};
      const auto expected = DrawLight(game, game, light, run);
      const auto got = DrawLight(ours, game, light, run);
      const auto falloff = DrawLight(other, other, light, run);
      CHECK(expected.size() == 16 && got.size() == 16 && falloff.size() == 16, "%08X: the draws", hash);
      if (expected.size() != 16 || got.size() != 16 || falloff.size() != 16) return;
      for (size_t pixel = 0; pixel < 16; ++pixel) {
        for (size_t channel = 0; channel < 4; ++channel) {
          ++compared;
          const float difference = std::fabs(expected[pixel][channel] - got[pixel][channel]);
          largest = std::max(largest, difference);
          if (!Near(expected[pixel][channel], got[pixel][channel], 1e-4f) && ++misses <= 4) {
            std::printf("  %08X light %u pixel %zu channel %zu: the game's %.7g, ours %.7g\n", hash, run, pixel, channel, expected[pixel][channel],
                        got[pixel][channel]);
          }
          lit += (channel < 3 && expected[pixel][channel] > 0.f) ? 1 : 0;
          control += Near(expected[pixel][channel], falloff[pixel][channel], 1e-3f) ? 0 : 1;
        }
      }
    }
  }
  CHECK(misses == 0 && lit > compared / 8 && control > 0, "point lights: %d values, %d misses, %d lit, CONTROL %d differ", compared, misses, lit,
        control);
  std::printf("point lights: pointlight00 / 01 (nothing at t42) against D0C67938 / 5D1695BC, %d values, %d misses (largest %.3g), %d lit; CONTROL the "
              "other falloff: %d differ\n",
              compared, misses, largest, lit, control);
}

// The forward readers' constants by name (the game's: shadowmap_PS, camera, lighting_VS_PS, shroud_PS), from `at`
// floats into the variable: each cascade the whole 4 x 4 atlas at depth 0.5 / w (w = 1 + (x + y + z) / 2: a symmetric
// matrix, the same in either layout), the first split up to 1e6 (the cascade at any depth 0), no bias or fade; no clip
// rectangle, cloud shadow or shroud.
struct NamedConstant {
  const char* name;
  UINT at;
  std::vector<float> values;
};
static const std::vector<float> SPLIT_MATRIX = {0.f, 0.f, 0.f, 0.5f, 0.f, 0.f, 0.f, 0.5f, 0.f, 0.f, 0.f, 0.5f, 0.5f, 0.5f, 0.5f, 1.f};
static const std::vector<NamedConstant> FORWARD_CONSTANTS = {
    {"g_vHardShadowBufferSize", 0, {4.f, 4.f, 0.25f, 0.25f}},
    {"g_amHardSplit", 0, SPLIT_MATRIX},
    {"g_amHardSplit", 16, SPLIT_MATRIX},
    {"g_amHardSplit", 32, SPLIT_MATRIX},
    {"g_amHardSplit", 48, SPLIT_MATRIX},
    {"g_fFadeRange", 0, {0.1f, 0.1f}},
    {"g_sample_bias", 0, {0.f}},
    {"g_iSplitCount", 0, {4.f}},
    {"g_split_distances", 0, {0.f}},
    {"g_split_distances", 4, {1e6f}},
    {"g_split_distances", 8, {1e6f}},
    {"g_split_distances", 12, {1e6f}},
    {"g_split_distances", 16, {1e6f}},
    {"g_split_uv_texture_bounds", 0, {0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 1.f, 1.f}},
    {"g_clip_rect", 0, {-1e30f, -1e30f, 1e30f, 1e30f}},
    {"g_cloud_shadow_lerp", 0, {0.f}},
    {"g_shroud_enabled", 0, {0.f}},
};

struct ForwardScene {
  float fill = 0.5f;
  float sun = 1.f;         // sun_direction (sun, sun, sun) / sqrt 3 (the readers differ in which way it points)
  bool glint = false;      // the camera far along (1, 1, 0) and the sun along -(1, 1, 0): a +x normal reflects it (the battle
                           // water's sun glint, its only shadowed light)
  float height = 0.f;      // added to every float input's y (the battle water's world place above its ground)
  float input_w = 1.f;     // every float input's w (the leaves' fade)
  bool flat = false;       // every float input's y the same at every pixel (no ramp)
  float ground = NAN;      // inv_view_projection's rows 1 and 3: the depth buffer's world y (NaN: the fill)
  float scatter = -1.f;    // g_sea_bed_light_scatter's 4 dwords (< 0: the fill)
  float atlas = 1.f;       // the shadow map's depth
  float occlusion = -1.f;  // t49 (< 0: unbound)
  float caustics = -1.f;   // t51 (< 0: unbound)
};

// A forward pixel shader's bytes drawn over 4 x 4 pixels: its inputs from a vertex shader made from its input signature
// (floats pixel-varying values, integers 0), every resource it binds by reflection (`layout`: the game's bytes) filled
// with `scene.fill` (2D textures 4 x 4, cubes, arrays; structured buffers 16 elements: the particles' first 4 uints
// m_behaviour_mask receive shadows 0x40000 and lit 0x80000, the other flags 0) but FORWARD_CONSTANTS and sun_direction,
// its comparison samplers LESS_EQUAL, the others
// linear; its shadow map (a name with ShadowBuffer) all `scene.atlas`; t49 `scene.occlusion`. Every target it writes.
static std::vector<Float4> DrawForward(std::span<const uint8_t> code, std::span<const uint8_t> layout, const ForwardScene& scene) {
  const float fill = scene.fill;
  ComPtr<ID3D11ShaderReflection> reflection;
  if (FAILED(D3DReflect(layout.data(), layout.size(), IID_ID3D11ShaderReflection, reinterpret_cast<void**>(reflection.GetAddressOf())))) return {};
  D3D11_SHADER_DESC desc = {};
  reflection->GetDesc(&desc);
  std::string vertex = "void main(uint id : SV_VertexID";
  std::string body = " const float2 corner = float2((id << 1) & 2, id & 2); position = float4(corner * float2(2, -2) + float2(-1, 1), 0, 1);";
  for (UINT index = 0; index < desc.InputParameters; ++index) {
    D3D11_SIGNATURE_PARAMETER_DESC input = {};
    reflection->GetInputParameterDesc(index, &input);
    if (input.SystemValueType == D3D_NAME_POSITION) {
      vertex += ", out float4 position : SV_Position";
      continue;
    }
    if (input.SystemValueType != D3D_NAME_UNDEFINED) continue;
    const std::string name = "v" + std::to_string(index);
    const bool integer = input.ComponentType != D3D_REGISTER_COMPONENT_FLOAT32;
    vertex += std::string(", out ") + (input.ComponentType == D3D_REGISTER_COMPONENT_SINT32 ? "int4 " : (integer ? "uint4 " : "float4 ")) + name + " : " + input.SemanticName + std::to_string(input.SemanticIndex);
    body += " " + name + (integer ? " = 0;" : " = float4(corner.x * 0.31 + " + std::to_string(0.13f * static_cast<float>(index)) + ", corner.y * " +
                                                    (scene.flat ? "0" : "0.27") + " + 0.2, " +
                                                    std::to_string(0.5f + (0.05f * static_cast<float>(index))) + ", " + std::to_string(scene.input_w) +
                                                    ") + float4(0, " +
                                                    std::to_string(scene.height) + ", 0, 0);");
  }
  const auto vertex_code = CompileText(vertex + ") {" + body + " }", "forward vertex", "main", "vs_5_0");
  ComPtr<ID3D11VertexShader> vertex_shader;
  ComPtr<ID3D11PixelShader> pixel_shader;
  device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, vertex_shader.GetAddressOf());
  if (FAILED(device->CreatePixelShader(code.data(), code.size(), nullptr, pixel_shader.GetAddressOf()))) return {};
  std::vector<ComPtr<ID3D11Buffer>> buffers;
  std::vector<ComPtr<ID3D11ShaderResourceView>> views;
  std::vector<ComPtr<ID3D11SamplerState>> samplers;
  for (UINT index = 0; index < desc.BoundResources; ++index) {
    D3D11_SHADER_INPUT_BIND_DESC bind = {};
    reflection->GetResourceBindingDesc(index, &bind);
    const std::string name = bind.Name;
    if (bind.Type == D3D_SIT_CBUFFER) {
      auto* constants = reflection->GetConstantBufferByName(bind.Name);
      D3D11_SHADER_BUFFER_DESC buffer_desc = {};
      constants->GetDesc(&buffer_desc);
      std::vector<float> data(((buffer_desc.Size + 15) & ~15u) / 4, fill);
      for (UINT variable = 0; variable < buffer_desc.Variables; ++variable) {
        D3D11_SHADER_VARIABLE_DESC variable_desc = {};
        constants->GetVariableByIndex(variable)->GetDesc(&variable_desc);
        for (const auto& constant : FORWARD_CONSTANTS) {
          const size_t at = (variable_desc.StartOffset / 4) + constant.at;
          if (std::strcmp(constant.name, variable_desc.Name) == 0 && at + constant.values.size() <= data.size()) {
            std::ranges::copy(constant.values, data.begin() + static_cast<ptrdiff_t>(at));
          }
        }
        const auto at = data.begin() + static_cast<ptrdiff_t>(variable_desc.StartOffset / 4);
        if (std::strcmp(variable_desc.Name, "sun_direction") == 0) {
          std::fill_n(at, 3, scene.sun * 0.57735027f);
          if (scene.glint) std::ranges::copy(std::array{-0.70710678f, -0.70710678f, 0.f}, at);
        }
        if (std::strcmp(variable_desc.Name, "camera_position") == 0 && scene.glint) std::ranges::copy(std::array{707.f, 707.f, 0.f}, at);
        if (std::strcmp(variable_desc.Name, "inv_view_projection") == 0 && !std::isnan(scene.ground)) {
          std::ranges::copy(std::array{0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, scene.ground, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f}, at);
        }
        if (std::strcmp(variable_desc.Name, "g_sea_bed_light_scatter") == 0 && scene.scatter >= 0.f) std::fill_n(at, 4, scene.scatter);
      }
      buffers.push_back(Constants(data.data(), static_cast<UINT>(data.size() * 4)));
      context->PSSetConstantBuffers(bind.BindPoint, 1, buffers.back().GetAddressOf());
    } else if (bind.Type == D3D_SIT_SAMPLER) {
      const bool comparison = (bind.uFlags & D3D_SIF_COMPARISON_SAMPLER) != 0;
      const D3D11_SAMPLER_DESC sampler = {.Filter = comparison ? D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_LINEAR,
                                          .AddressU = D3D11_TEXTURE_ADDRESS_WRAP, .AddressV = D3D11_TEXTURE_ADDRESS_WRAP, .AddressW = D3D11_TEXTURE_ADDRESS_WRAP,
                                          .ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL, .MaxLOD = D3D11_FLOAT32_MAX};
      samplers.emplace_back();
      device->CreateSamplerState(&sampler, samplers.back().GetAddressOf());
      context->PSSetSamplers(bind.BindPoint, 1, samplers.back().GetAddressOf());
    } else if (bind.Type == D3D_SIT_STRUCTURED) {
      // bind.NumSamples: its stride.
      std::vector<float> elements(16 * bind.NumSamples / 4, fill);
      for (size_t element = 0; element < 16; ++element) {
        const std::array<uint32_t, 4> flags = {0x40000 | 0x80000, 0, 0, 0};
        std::memcpy(&elements[element * bind.NumSamples / 4], flags.data(), sizeof(flags));
      }
      const D3D11_BUFFER_DESC buffer_desc = {.ByteWidth = static_cast<UINT>(elements.size() * 4), .Usage = D3D11_USAGE_DEFAULT,
                                             .BindFlags = D3D11_BIND_SHADER_RESOURCE, .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
                                             .StructureByteStride = bind.NumSamples};
      const D3D11_SUBRESOURCE_DATA buffer_data = {.pSysMem = elements.data()};
      ComPtr<ID3D11Buffer> buffer;
      device->CreateBuffer(&buffer_desc, &buffer_data, buffer.GetAddressOf());
      views.emplace_back();
      device->CreateShaderResourceView(buffer.Get(), nullptr, views.back().GetAddressOf());
      context->PSSetShaderResources(bind.BindPoint, 1, views.back().GetAddressOf());
    } else if (bind.Type == D3D_SIT_TEXTURE && name.find("ShadowBuffer") != std::string::npos) {
      // The atlas as the game's: one R32 float (comparisons need it).
      const std::vector<float> depths(16, scene.atlas);
      const D3D11_TEXTURE2D_DESC atlas_desc = {.Width = 4, .Height = 4, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32_FLOAT,
                                               .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
      const D3D11_SUBRESOURCE_DATA atlas_data = {.pSysMem = depths.data(), .SysMemPitch = 16};
      ComPtr<ID3D11Texture2D> texture;
      device->CreateTexture2D(&atlas_desc, &atlas_data, texture.GetAddressOf());
      views.emplace_back();
      device->CreateShaderResourceView(texture.Get(), nullptr, views.back().GetAddressOf());
      context->PSSetShaderResources(bind.BindPoint, 1, views.back().GetAddressOf());
    } else if (bind.Type == D3D_SIT_TEXTURE) {
      std::vector<Float4> texels(16 * 6);
      for (size_t texel = 0; texel < texels.size(); ++texel) {
        const float vary = 0.05f * static_cast<float>(texel % 7);
        texels[texel] = {fill + vary, fill, fill - vary, 1.f};
      }
      const bool cube = bind.Dimension == D3D_SRV_DIMENSION_TEXTURECUBE || bind.Dimension == D3D_SRV_DIMENSION_TEXTURECUBEARRAY;
      const bool array = bind.Dimension == D3D_SRV_DIMENSION_TEXTURE2DARRAY;
      if (!cube && !array && bind.Dimension != D3D_SRV_DIMENSION_TEXTURE2D) continue;
      const auto texture = Texture(4, 4, texels.data(), D3D11_BIND_SHADER_RESOURCE, (cube || array) ? 6 : 1, cube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0);
      D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {.Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .ViewDimension = bind.Dimension};
      if (bind.Dimension == D3D_SRV_DIMENSION_TEXTURE2D) view_desc.Texture2D = {0, 1};
      if (bind.Dimension == D3D_SRV_DIMENSION_TEXTURE2DARRAY) view_desc.Texture2DArray = {0, 1, 0, 6};
      if (bind.Dimension == D3D_SRV_DIMENSION_TEXTURECUBE) view_desc.TextureCube = {0, 1};
      if (bind.Dimension == D3D_SRV_DIMENSION_TEXTURECUBEARRAY) view_desc.TextureCubeArray = {0, 1, 0, 1};
      views.emplace_back();
      device->CreateShaderResourceView(texture.Get(), &view_desc, views.back().GetAddressOf());
      context->PSSetShaderResources(bind.BindPoint, 1, views.back().GetAddressOf());
    }
  }
  if (scene.occlusion >= 0.f) {
    views.push_back(View(4, 4, std::vector<Float4>(16, Float4{scene.occlusion, scene.occlusion, scene.occlusion, scene.occlusion})));
    context->PSSetShaderResources(49, 1, views.back().GetAddressOf());
  }
  if (scene.caustics >= 0.f) {
    views.push_back(View(4, 4, std::vector<Float4>(16, Float4{scene.caustics, scene.caustics, scene.caustics, scene.caustics})));
    context->PSSetShaderResources(water_patch::WATER_CAUSTICS_SLOT, 1, views.back().GetAddressOf());
  }
  std::vector<ComPtr<ID3D11Texture2D>> targets;
  std::vector<ID3D11RenderTargetView*> rtvs;
  for (UINT index = 0; index < desc.OutputParameters; ++index) {
    D3D11_SIGNATURE_PARAMETER_DESC output = {};
    reflection->GetOutputParameterDesc(index, &output);
    if (output.SystemValueType != D3D_NAME_TARGET) continue;
    targets.push_back(Texture(4, 4, nullptr, D3D11_BIND_RENDER_TARGET));
    rtvs.resize(std::max<size_t>(rtvs.size(), output.SemanticIndex + 1), nullptr);
    device->CreateRenderTargetView(targets.back().Get(), nullptr, &rtvs[output.SemanticIndex]);
  }
  context->VSSetShader(vertex_shader.Get(), nullptr, 0);
  context->PSSetShader(pixel_shader.Get(), nullptr, 0);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D11_VIEWPORT viewport = {.Width = 4.f, .Height = 4.f, .MaxDepth = 1.f};
  context->RSSetViewports(1, &viewport);
  context->OMSetRenderTargets(static_cast<UINT>(rtvs.size()), rtvs.data(), nullptr);
  context->Draw(3, 0);
  context->ClearState();
  for (auto* rtv : rtvs) {
    if (rtv != nullptr) rtv->Release();
  }
  std::vector<Float4> texels;
  for (const auto& target : targets) {
    const auto read = Read(target.Get());
    texels.insert(texels.end(), read.begin(), read.end());
  }
  return texels;
}

static void TestForwardShadows(const std::string& dump) {
  const auto same_bits = [](const std::vector<Float4>& a, const std::vector<Float4>& b) {
    return !a.empty() && a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(Float4)) == 0;
  };
  int patched = 0;
  int identical = 0;
  int failing_same = 0;
  int controls = 0;
  for (const uint32_t hash : {0x44B4A2F0u, 0x0BF2DB7Fu, 0xBCB9B1D6u, 0x7865CDC9u, 0x122F218Cu, 0x1A3FE75Fu, 0x0FDF70BFu}) {
    const auto game = GameBytes(dump, hash);
    const auto ours = ambient_patch::Patch(game, true, 1, true);
    if (game.empty() || ours.empty()) {
      std::printf("  %08X: %s\n", hash, game.empty() ? "no bytes" : "not patched");
      continue;
    }
    ++patched;
    // The scene that reaches its shadow (the atlas's value changes its output): the first of these fills, suns, glints,
    // heights.
    ForwardScene lit = {.fill = -1.f};
    for (const float height : {0.f, 4.f}) {
      for (const bool glint : {false, true}) {
        for (const float fill : {0.5f, 0.25f, 1.f, 0.1f}) {
          for (const float sun : {1.f, -1.f}) {
            const ForwardScene scene = {.fill = fill, .sun = sun, .glint = glint, .height = height, .atlas = 0.f};
            ForwardScene other = scene;
            other.atlas = 1.f;
            if (lit.fill < 0.f && !same_bits(DrawForward(game, game, scene), DrawForward(game, game, other))) lit = other;
          }
        }
      }
    }
    if (lit.fill < 0.f) {
      std::printf("  %08X: no fill reaches its shadow\n", hash);
      continue;
    }
    ++controls;
    ForwardScene shadowed = lit;
    shadowed.atlas = 0.f;
    ForwardScene blocked_by_rays = lit;
    blocked_by_rays.occlusion = 1.f;
    const bool same = same_bits(DrawForward(ours, game, lit), DrawForward(game, game, lit))
                      && same_bits(DrawForward(ours, game, shadowed), DrawForward(game, game, shadowed));
    const bool blocked = same_bits(DrawForward(ours, game, blocked_by_rays), DrawForward(game, game, shadowed));
    identical += same ? 1 : 0;
    failing_same += blocked ? 1 : 0;
    std::printf("  %08X (fill %g, sun %g%s, height %g): t49 unbound %s; t49 = 1 over a lit map %s every comparison failing\n", hash, lit.fill,
                lit.sun, lit.glint ? ", glint" : "", lit.height, same ? "bit-identical" : "DIFFERS", blocked ? "equals" : "DIFFERS from");
  }
  CHECK(patched == 7 && controls == 7 && identical == 7 && failing_same == 7, "forward shadows: %d patched, %d controls, %d identical, %d blocked",
        patched, controls, identical, failing_same);
  std::printf("forward shadows: %d of 7 patched; t49 unbound bit-identical %d, t49 = 1 equal to every comparison failing %d; CONTROL the map's value "
              "changes the game's output in %d\n",
              patched, identical, failing_same, controls);
}

// The guides' four outputs (diffuse albedo, specular albedo, normal, roughness) for one G-buffer texel, as
// anti_aliasing.hpp binds them: t0-t2 channels 1-3, u0-u3 (unorm 16-bit, float, unorm 16-bit) of 4 x 4.
static std::array<Float4, 4> RunGuides(ID3D11ComputeShader* shader, const SunScene& scene) {
  const std::array<ComPtr<ID3D11ShaderResourceView>, 3> channels = {
      View(4, 4, std::vector<Float4>(16, Float4{scene.normal[0], scene.normal[1], scene.gloss, 0.f})),
      View(4, 4, std::vector<Float4>(16, Float4{scene.albedo, scene.albedo, scene.albedo, 0.f})),
      View(4, 4, std::vector<Float4>(16, Float4{scene.specular, scene.specular, scene.specular, 0.f}))};
  constexpr std::array<DXGI_FORMAT, 4> FORMATS = {DXGI_FORMAT_R16G16B16A16_UNORM, DXGI_FORMAT_R16G16B16A16_UNORM, DXGI_FORMAT_R32G32B32A32_FLOAT,
                                                  DXGI_FORMAT_R16_UNORM};
  std::array<ComPtr<ID3D11Texture2D>, 4> targets;
  std::array<ComPtr<ID3D11UnorderedAccessView>, 4> uavs;
  for (size_t index = 0; index < 4; ++index) {
    const D3D11_TEXTURE2D_DESC desc = {.Width = 4, .Height = 4, .MipLevels = 1, .ArraySize = 1, .Format = FORMATS[index], .SampleDesc = {.Count = 1},
                                       .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_UNORDERED_ACCESS};
    device->CreateTexture2D(&desc, nullptr, targets[index].GetAddressOf());
    device->CreateUnorderedAccessView(targets[index].Get(), nullptr, uavs[index].GetAddressOf());
  }
  std::array<ID3D11ShaderResourceView*, 3> views = {channels[0].Get(), channels[1].Get(), channels[2].Get()};
  std::array<ID3D11UnorderedAccessView*, 4> unordered = {uavs[0].Get(), uavs[1].Get(), uavs[2].Get(), uavs[3].Get()};
  context->CSSetShader(shader, nullptr, 0);
  context->CSSetShaderResources(0, 3, views.data());
  context->CSSetUnorderedAccessViews(0, 4, unordered.data(), nullptr);
  context->Dispatch(1, 1, 1);
  context->ClearState();
  // Texel (1, 1) of each, as floats.
  std::array<Float4, 4> texel = {};
  for (size_t index = 0; index < 4; ++index) {
    D3D11_TEXTURE2D_DESC desc = {};
    targets[index]->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf());
    context->CopyResource(staging.Get(), targets[index].Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    const uint8_t* row = static_cast<const uint8_t*>(mapped.pData) + mapped.RowPitch;
    if (FORMATS[index] == DXGI_FORMAT_R32G32B32A32_FLOAT) {
      std::memcpy(texel[index].data(), row + 16, 16);
    } else {
      const size_t components = (FORMATS[index] == DXGI_FORMAT_R16_UNORM) ? 1 : 4;
      for (size_t component = 0; component < components; ++component) {
        uint16_t value = 0;
        std::memcpy(&value, row + (components * 2) + (component * 2), 2);
        texel[index][component] = static_cast<float>(value) / 65535.f;
      }
    }
    context->Unmap(staging.Get(), 0);
  }
  return texel;
}

static void TestGuides(const std::string& games, const std::string& dump) {
  const auto bytes = GameBytes(dump, 0x230F2241);
  const std::string path = games + "/twwarhammer/aa_rr_guides.cs_5_0.hlsl";
  const auto guides = ComputeShader(ReadText(path), path);
  const std::string shared_path = games + "/twwarhammer2/aa_rr_guides.cs_5_0.hlsl";
  const std::string shared = ReadText(shared_path);
  const auto swapped = ComputeShader(Without(shared, "normal.xy * float2(", "normal.yx * float2("), shared_path);
  const auto no_specular = ComputeShader(Without(shared, "gbuffer_colour[id.xy].rgb * (1.f - specular)", "gbuffer_colour[id.xy].rgb"), shared_path);
  // Sun 1 along `axis` (the way to it), no ambient.
  const auto lit_from = [](size_t axis) {
    Lighting lighting;
    lighting.c[1 + axis] = -1.f;
    std::fill_n(lighting.c.begin() + 4, 3, 1.f);
    lighting.c[44] = 1.f;
    lighting.c[51] = 1.f;
    return lighting;
  };
  Lighting sky;
  for (size_t side = 0; side < 6; ++side) std::fill_n(sky.c.begin() + 8 + (side * 4), 3, 1.f);
  sky.c[44] = 1.f;
  sky.c[51] = 1.f;
  int normal_misses = 0;
  int albedo_misses = 0;
  int roughness_misses = 0;
  int swapped_differ = 0;
  int unspecular_differ = 0;
  float largest = 0.f;
  const std::array<std::array<float, 2>, 6> normals = {{{0.62f, 0.55f}, {0.71f, 0.6f}, {0.83f, 0.53f}, {0.58f, 0.7f}, {0.9f, 0.66f}, {0.66f, 0.72f}}};
  for (const auto& normal : normals) {
    std::array<float, 3> lit = {};
    for (size_t axis = 0; axis < 3; ++axis) lit[axis] = PerAlbedo(bytes, {.lighting = lit_from(axis), .normal = normal, .gloss = 0.3f})[0];
    const float length = std::sqrt((lit[0] * lit[0]) + (lit[1] * lit[1]) + (lit[2] * lit[2]));
    const auto ours = RunGuides(guides.Get(), {.albedo = 0.5f, .normal = normal, .gloss = 0.3f, .specular = 0.3f});
    for (size_t axis = 0; axis < 3; ++axis) {
      const float miss = std::fabs((lit[axis] / length) - ours[2][axis]);
      largest = std::max(largest, miss);
      normal_misses += (miss > 1e-4f) ? 1 : 0;
    }
    roughness_misses += (std::fabs(ours[3][0] - 0.7f) > 1e-4f) ? 1 : 0;
    const auto other = RunGuides(swapped.Get(), {.albedo = 0.5f, .normal = normal, .gloss = 0.3f, .specular = 0.3f});
    swapped_differ += (std::fabs(other[2][0] - ours[2][0]) + std::fabs(other[2][1] - ours[2][1]) > 1e-3f) ? 1 : 0;
    // The ambient per unit albedo with specular colour 0.3 over the one with 0: the game's (1 - specular colour).
    const float game_share = PerAlbedo(bytes, {.lighting = sky, .normal = normal, .specular = 0.3f})[0] / PerAlbedo(bytes, {.lighting = sky, .normal = normal})[0];
    albedo_misses += (std::fabs((ours[0][0] / 0.5f) - game_share) > 1e-3f) ? 1 : 0;
    const auto plain = RunGuides(no_specular.Get(), {.albedo = 0.5f, .normal = normal, .gloss = 0.3f, .specular = 0.3f});
    unspecular_differ += (std::fabs((plain[0][0] / 0.5f) - game_share) > 1e-2f) ? 1 : 0;
  }
  CHECK(normal_misses == 0 && albedo_misses == 0 && roughness_misses == 0, "RR guides: normal %d, diffuse albedo %d, roughness %d misses", normal_misses,
        albedo_misses, roughness_misses);
  CHECK(swapped_differ == 6 && unspecular_differ == 6, "RR guides CONTROLS: angles swapped %d, no specular colour %d of 6 differ", swapped_differ,
        unspecular_differ);
  std::printf("RR guides against 230F2241's decode, 6 normals: normal %d misses of 18 (largest %.3g), diffuse albedo %d, roughness %d misses; "
              "CONTROLS angles swapped %d, no specular colour %d of 6 differ\n",
              normal_misses, largest, albedo_misses, roughness_misses, swapped_differ, unspecular_differ);
}

static void TestThin(const std::string& dump) {
  const auto position_first = [&dump](uint32_t hash) {
    const auto code = GameBytes(dump, hash);
    ComPtr<ID3D11ShaderReflection> reflection;
    if (code.empty() || FAILED(D3DReflect(code.data(), code.size(), IID_ID3D11ShaderReflection, reinterpret_cast<void**>(reflection.GetAddressOf())))) {
      return false;
    }
    D3D11_SIGNATURE_PARAMETER_DESC output = {};
    reflection->GetOutputParameterDesc(0, &output);
    return output.SystemValueType == D3D_NAME_POSITION && output.Register == 0;
  };
  const bool banners = position_first(0xDBCC30FD) && position_first(0xE7F0D71A);
  const bool imposter = position_first(0x381E079E);
  const auto bytes = GameBytes(dump, 0x230F2241);
  const auto diffuse = [&bytes](float albedo) {
    return DrawGame(bytes, {.lighting = SunLighting({1.f, 1.f, 1.f, 0.f}, {}, 1.f), .albedo = albedo})[0];
  };
  const float low = (diffuse(0.3f) - diffuse(0.1f)) / 0.2f;
  const float high = (diffuse(0.9f) - diffuse(0.3f)) / 0.6f;
  CHECK(banners && !imposter, "thin: the banners' SV_Position first %d, CONTROL the imposter's %d", banners, imposter);
  CHECK(low > 0.f && Near(high / low, 1.f, 1e-4f), "thin: 230F2241's diffuse slopes %.8g, %.8g (linear: equal)", low, high);
  std::printf("Light Through All Banners: DBCC30FD / E7F0D71A output SV_Position first %s (CONTROL the imposter's %s); 230F2241's diffuse "
              "slopes %.8g / %.8g (ratio %.6f: linear, albedo_power 1)\n",
              banners ? "yes" : "NO", imposter ? "YES" : "no", low, high, high / low);
}

static void TestFoliage(const std::string& dump) {
  // Channel 3's alpha (target 2) at pixel 5 of a G-buffer shader drawn as part 8 draws it.
  const auto flags_written = [&dump](uint32_t hash) {
    const auto code = GameBytes(dump, hash);
    const auto texels = DrawForward(code, code, {.glint = true, .input_w = 0.f});
    return (texels.size() >= 48) ? texels[32 + 5][3] * 255.f : -1.f;
  };
  const float leaves = flags_written(0x744FDFF0);
  const float branches = flags_written(0x4CF47121);
  const auto bytes = GameBytes(dump, 0x230F2241);
  Lighting behind;
  behind.c[1] = 1.f;  // the sun shines along +x onto a +x normal: from behind
  std::fill_n(behind.c.begin() + 4, 3, 1.f);
  behind.c[44] = 1.f;
  behind.c[51] = 1.f;
  std::array<float, 4> through = {};
  constexpr std::array<float, 4> FLAGS = {128.f, 0.f, 64.f, 16.f};
  for (size_t index = 0; index < FLAGS.size(); ++index) through[index] = DrawGame(bytes, {.lighting = behind, .flags = FLAGS[index]})[0];
  CHECK(std::fabs(leaves - 128.f) < 1e-3f && std::fabs(branches) < 1e-3f, "foliage: leaves write %g, CONTROL branches %g", leaves, branches);
  CHECK(through[0] > 1e-6f && through[1] == 0.f && through[2] == 0.f && through[3] == 0.f, "foliage: through with flags 128 %g, 0 %g, 64 %g, 16 %g",
        through[0], through[1], through[2], through[3]);
  std::printf("Light Through Foliage: 744FDFF0 writes flags %g (CONTROL 4CF47121 %g); 230F2241 from behind: flags 128 %g, "
              "CONTROLS 0 %g, 64 %g, 16 %g\n",
              leaves, branches, through[0], through[1], through[2], through[3]);
}

static void TestWater(const std::string& games, const std::string& dump) {
  constexpr water_patch::WaterShader WATER = {.hash = 0x44B4A2F0, .lighting_slot = 3, .world_input = 3, .refraction_slot = 4, .normal_slot = 3};
  constexpr float CLARITY = 2.f;
  constexpr float SCATTER = 0.3f;
  // water_clarity.cs over 72 dwords (lighting_VS_PS's 18 registers) of a ramp.
  const std::string path = games + "/twwarhammer/water_clarity.cs_5_0.hlsl";
  const auto clarity = ComputeShader(ReadText(path), path);
  std::vector<float> lighting(72);
  for (size_t index = 0; index < lighting.size(); ++index) lighting[index] = 0.05f + (0.01f * static_cast<float>(index));
  const auto raw = [](const void* data, UINT bytes, UINT bind) {
    const D3D11_BUFFER_DESC desc = {.ByteWidth = bytes, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = bind, .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS};
    const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = data};
    ComPtr<ID3D11Buffer> buffer;
    device->CreateBuffer(&desc, (data != nullptr) ? &initial : nullptr, buffer.GetAddressOf());
    return buffer;
  };
  const auto source = raw(lighting.data(), 288, D3D11_BIND_SHADER_RESOURCE);
  const auto output = raw(nullptr, 288, D3D11_BIND_UNORDERED_ACCESS);
  const D3D11_SHADER_RESOURCE_VIEW_DESC source_desc = {.Format = DXGI_FORMAT_R32_TYPELESS, .ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX,
                                                       .BufferEx = {.FirstElement = 0, .NumElements = 72, .Flags = D3D11_BUFFEREX_SRV_FLAG_RAW}};
  const D3D11_UNORDERED_ACCESS_VIEW_DESC output_desc = {.Format = DXGI_FORMAT_R32_TYPELESS, .ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
                                                        .Buffer = {.FirstElement = 0, .NumElements = 72, .Flags = D3D11_BUFFER_UAV_FLAG_RAW}};
  ComPtr<ID3D11ShaderResourceView> source_view;
  ComPtr<ID3D11UnorderedAccessView> output_view;
  device->CreateShaderResourceView(source.Get(), &source_desc, source_view.GetAddressOf());
  device->CreateUnorderedAccessView(output.Get(), &output_desc, output_view.GetAddressOf());
  const float inverse = 1.f / CLARITY;
  std::array<uint32_t, 4> constants = {72, 0, 0, 0};
  std::memcpy(&constants[1], &inverse, 4);
  const auto clarity_constants = Constants(constants.data(), 16);
  context->CSSetShader(clarity.Get(), nullptr, 0);
  context->CSSetShaderResources(0, 1, source_view.GetAddressOf());
  context->CSSetUnorderedAccessViews(0, 1, output_view.GetAddressOf(), nullptr);
  context->CSSetConstantBuffers(0, 1, clarity_constants.GetAddressOf());
  context->Dispatch(2, 1, 1);
  context->ClearState();
  const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = 288, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
  ComPtr<ID3D11Buffer> staging;
  device->CreateBuffer(&staging_desc, nullptr, staging.GetAddressOf());
  context->CopyResource(staging.Get(), output.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  std::vector<float> clarified(72);
  std::memcpy(clarified.data(), mapped.pData, 288);
  context->Unmap(staging.Get(), 0);
  int copy_misses = 0;
  for (size_t index = 0; index < lighting.size(); ++index) {
    const bool scatter = index >= 40 && index < 44;
    const bool same = scatter ? Near(clarified[index], std::pow(lighting[index], inverse), 1e-5f)
                              : std::memcmp(&clarified[index], &lighting[index], 4) == 0;
    copy_misses += same ? 0 : 1;
  }
  // The game's water with that c10 a metre deep against its own half a metre deep: a flat surface 4.2 m up, the ground below.
  const auto game = GameBytes(dump, WATER.hash);
  const ForwardScene metre = {.height = 4.f, .flat = true, .ground = 3.2f, .scatter = std::pow(SCATTER, inverse)};
  ForwardScene half = metre;
  half.ground = 3.7f;
  half.scatter = SCATTER;
  ForwardScene plain_metre = metre;
  plain_metre.scatter = SCATTER;
  const auto clear = DrawForward(game, game, metre);
  const auto shallow = DrawForward(game, game, half);
  const auto murky = DrawForward(game, game, plain_metre);
  int depth_misses = 0;
  int control_differ = 0;
  for (size_t texel = 0; texel < clear.size() && clear.size() == shallow.size(); ++texel) {
    for (size_t channel = 0; channel < 4; ++channel) {
      depth_misses += Near(clear[texel][channel], shallow[texel][channel], 1e-4f) ? 0 : 1;
      control_differ += Near(murky[texel][channel], shallow[texel][channel], 1e-4f) ? 0 : 1;
    }
  }
  // Caustics: on the game's bytes and on top of part 8's forward patch.
  const auto caustic = water_patch::PatchWaterCaustics(game, WATER);
  const auto forward = ambient_patch::Patch(game, true, 1, true);
  const auto chained = water_patch::PatchWaterCaustics(forward, WATER);
  ForwardScene lit = metre;
  lit.scatter = SCATTER;
  ForwardScene with_tile = lit;
  with_tile.caustics = 1.f;
  const auto same_bits = [](const std::vector<Float4>& a, const std::vector<Float4>& b) {
    return !a.empty() && a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(Float4)) == 0;
  };
  const bool unbound = same_bits(DrawForward(caustic, game, lit), DrawForward(game, game, lit))
                       && same_bits(DrawForward(chained, game, lit), DrawForward(forward, game, lit));
  const bool tiled = !same_bits(DrawForward(caustic, game, with_tile), DrawForward(game, game, lit));
  CHECK(copy_misses == 0 && !clear.empty() && depth_misses == 0, "water clarity: copy %d, depth %d misses", copy_misses, depth_misses);
  CHECK(control_differ > 0, "water clarity CONTROL: the game's c10 a metre deep the same as half");
  CHECK(!caustic.empty() && !chained.empty() && unbound && tiled, "water caustics: patched %d / chained %d, t51 unbound same %d, t51 = 1 differs %d",
        !caustic.empty(), !chained.empty(), unbound, tiled);
  std::printf("water: water_clarity.cs copy %d misses of 72; 44B4A2F0 with c10^(1/2) a metre deep vs half a metre: %d misses of %zu (CONTROL the game's "
              "c10 a metre deep: %d differ); caustics patched %s, on the forward patch %s, t51 unbound bit-identical %s, CONTROL t51 = 1 %s\n",
              copy_misses, depth_misses, clear.size() * 4, control_differ, caustic.empty() ? "NO" : "yes", chained.empty() ? "NO" : "yes",
              unbound ? "yes" : "NO", tiled ? "differs" : "SAME");
}

static void TestCutOuts(const std::string& root, const std::string& dump) {
  const std::string embed = root + "/build/twwarhammer.include/embed/";
  uint32_t state = 777u;
  const auto unit = [&state](float low, float high) {
    state = state * 1664525u + 1013904223u;
    return low + ((high - low) * static_cast<float>(state >> 8) / 16777216.f);
  };
  int values = 0;
  int misses = 0;
  int control = 0;
  for (const auto& [hash, name] : {std::pair{0x20A9E5FDu, "rt_capture_unit"}, std::pair{0x3FC5424Cu, "rt_capture_unit_4_bones"}}) {
    const auto game = GameBytes(dump, hash);
    const auto cut_out = Bytes(embed + name + "_cut_out.cso");
    const auto plain = Bytes(embed + name + ".cso");
    ComPtr<ID3D11ShaderReflection> reflection;
    if (game.empty() || FAILED(D3DReflect(game.data(), game.size(), IID_ID3D11ShaderReflection, reinterpret_cast<void**>(reflection.GetAddressOf())))) {
      CHECK(false, "cut-outs: %08X's bytes", hash);
      continue;
    }
    D3D11_SHADER_DESC desc = {};
    reflection->GetDesc(&desc);
    UINT floats = 0;
    for (UINT index = 0; index < desc.InputParameters; ++index) {
      D3D11_SIGNATURE_PARAMETER_DESC input = {};
      reflection->GetInputParameterDesc(index, &input);
      floats += static_cast<UINT>(std::bit_width(static_cast<UINT>(input.Mask)));
    }
    std::vector<float> vertices;
    for (int vertex = 0; vertex < 16; ++vertex) {
      for (UINT component = 0; component < floats; ++component) vertices.push_back(unit(0.f, 1.f));
    }
    std::array<float, 64 * 4> zeros = {};
    const auto buffer = Constants(zeros.data(), sizeof(zeros));
    const std::array<ID3D11Buffer*, 3> constants = {buffer.Get(), buffer.Get(), buffer.Get()};
    const std::array<Streamed, 1> game_uv = {{{"TEXCOORD", 1, 2}}};
    const std::array<Streamed, 1> capture_world = {{{"TEXCOORD", 0, 4}}};
    const auto expected = StreamOut(game, game, game_uv, vertices, constants);
    const auto ours = StreamOut(game, cut_out, capture_world, vertices, constants);
    const auto without = StreamOut(game, plain, capture_world, vertices, constants);
    CHECK(!expected.empty() && ours.size() == expected.size() * 2 && without.size() == ours.size(), "cut-outs: %08X's stream out", hash);
    if (expected.empty() || ours.size() != expected.size() * 2 || without.size() != ours.size()) continue;
    for (size_t vertex = 0; vertex < expected.size() / 2; ++vertex) {
      const auto halves = [](float packed) {
        uint32_t bits = 0;
        std::memcpy(&bits, &packed, 4);
        return std::array<float, 2>{HalfValue(bits & 0xFFFFu), HalfValue(bits >> 16)};
      };
      const auto uv = halves(ours[(vertex * 4) + 3]);
      const auto other = halves(without[(vertex * 4) + 3]);
      for (size_t axis = 0; axis < 2; ++axis) {
        ++values;
        misses += (std::fabs(uv[axis] - expected[(vertex * 2) + axis]) > 1e-3f) ? 1 : 0;
        control += (std::fabs(other[axis] - expected[(vertex * 2) + axis]) > 1e-3f) ? 1 : 0;
      }
    }
  }
  CHECK(values == 64 && misses == 0 && control > 0, "cut-outs: %d values, %d misses, CONTROL %d differ", values, misses, control);
  std::printf("cut-outs: rt_capture_unit(_4_bones)_cut_out against 20A9E5FD / 3FC5424C's TEXCOORD1, %d values, %d misses; CONTROL without the UV: %d "
              "differ\n",
              values, misses, control);
}

static void TestReflectionInputs(const std::string& games, const std::string& dump) {
  const std::string path = games + "/twwarhammer/shaders/ssr00_0x6B80E35B.cs_5_0.hlsl";
  const auto ours = ComputeShader(ReadText(path), path);
  const auto game_code = GameBytes(dump, 0x6B80E35B);
  ComPtr<ID3D11ComputeShader> game;
  if (game_code.empty() || FAILED(device->CreateComputeShader(game_code.data(), game_code.size(), nullptr, game.GetAddressOf()))) {
    CHECK(false, "reflection inputs: 6B80E35B's bytes");
    return;
  }
  constexpr float MARCH = 64000.f;
  const auto rays_first = RunResolve(ours.Get(), {.rays = true});
  const auto rays_second = RunResolve(ours.Get(), {.hit = MARCH, .radiance = 100.f, .rays = true});
  const auto ours_first = RunResolve(ours.Get(), {});
  const auto ours_second = RunResolve(ours.Get(), {.hit = MARCH, .radiance = 100.f});
  const auto game_first = RunResolve(game.Get(), {});
  const auto game_second = RunResolve(game.Get(), {.hit = MARCH, .radiance = 100.f});
  constexpr int PIXELS = 64 * 64;
  const auto differ = [](const Float4& a, const Float4& b) { return std::memcmp(a.data(), b.data(), 3 * sizeof(float)) != 0; };
  int rays_differ = 0;
  int rays_used = 0;
  int ours_differ = 0;
  int game_differ = 0;
  int faithful = 0;
  for (size_t at = 0; at < PIXELS; ++at) {
    rays_differ += differ(rays_first[at], rays_second[at]) ? 1 : 0;
    rays_used += differ(rays_first[at], ours_first[at]) ? 1 : 0;
    ours_differ += differ(ours_first[at], ours_second[at]) ? 1 : 0;
    game_differ += differ(game_first[at], game_second[at]) ? 1 : 0;
    bool same = true;
    for (size_t channel = 0; channel < 3; ++channel) same = same && Near(ours_first[at][channel], game_first[at][channel], 1e-4f);
    faithful += same ? 1 : 0;
  }
  CHECK(rays_differ == 0, "under the rays: %d of %d differ with other hits and light mips", rays_differ, PIXELS);
  CHECK(rays_used > PIXELS / 2, "the rays' reflections in the output: %d of %d", rays_used, PIXELS);
  CHECK(faithful == PIXELS, "the replacement without the rays: %d of %d as the game's bytes", faithful, PIXELS);
  CHECK(ours_differ > PIXELS / 2 && game_differ > PIXELS / 2, "CONTROLS without the rays: ours %d, the game's %d of %d differ", ours_differ,
        game_differ, PIXELS);
  std::printf("reflection inputs: under the rays %d of %d differ with other hits (t5) and light mips (t6), the rays in %d; without them the replacement "
              "%d of %d as the game's 6B80E35B; CONTROLS without the rays: ours %d, the game's %d differ\n",
              rays_differ, PIXELS, rays_used, faithful, PIXELS, ours_differ, game_differ);
}

static void TestAoAlone(const std::string& games, const std::string& dump) {
  constexpr UINT SIZE = 16;
  const std::string path = games + "/twattila/rt_ambient_combine.cs_5_0.hlsl";
  const auto combine = ComputeShader(ReadText(path), path);
  std::vector<float> traced(static_cast<size_t>(SIZE) * SIZE);
  for (size_t at = 0; at < traced.size(); ++at) traced[at] = static_cast<float>(at % 64) / 64.f;  // exact in half floats too
  const D3D11_TEXTURE2D_DESC traced_desc = {.Width = SIZE, .Height = SIZE, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32_FLOAT,
                                            .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  const D3D11_SUBRESOURCE_DATA traced_data = {.pSysMem = traced.data(), .SysMemPitch = SIZE * sizeof(float)};
  ComPtr<ID3D11Texture2D> traced_texture;
  ComPtr<ID3D11ShaderResourceView> traced_view;
  device->CreateTexture2D(&traced_desc, &traced_data, traced_texture.GetAddressOf());
  device->CreateShaderResourceView(traced_texture.Get(), nullptr, traced_view.GetAddressOf());
  const D3D11_SAMPLER_DESC linear = {.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
                                     .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP, .MaxLOD = D3D11_FLOAT32_MAX};
  ComPtr<ID3D11SamplerState> sampler;
  device->CreateSamplerState(&linear, sampler.GetAddressOf());
  const std::array<uint32_t, 4> size = {SIZE, SIZE, 0u, 0u};
  const auto size_buffer = Constants(size.data(), sizeof(size));
  // The game's AO target cleared to `game_value` (as the collect's skip clears it), then combined with the traced visibility.
  const auto combined = [&](float game_value) {
    const D3D11_TEXTURE2D_DESC ao_desc = {.Width = SIZE, .Height = SIZE, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R8_UNORM,
                                          .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT,
                                          .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS};
    ComPtr<ID3D11Texture2D> ao;
    ComPtr<ID3D11UnorderedAccessView> ao_uav;
    ComPtr<ID3D11ShaderResourceView> ao_view;
    device->CreateTexture2D(&ao_desc, nullptr, ao.GetAddressOf());
    device->CreateUnorderedAccessView(ao.Get(), nullptr, ao_uav.GetAddressOf());
    device->CreateShaderResourceView(ao.Get(), nullptr, ao_view.GetAddressOf());
    const std::array<float, 4> value = {game_value, game_value, game_value, game_value};
    context->ClearUnorderedAccessViewFloat(ao_uav.Get(), value.data());
    const auto output = Texture(SIZE, SIZE, nullptr, D3D11_BIND_UNORDERED_ACCESS);
    ComPtr<ID3D11UnorderedAccessView> uav;
    device->CreateUnorderedAccessView(output.Get(), nullptr, uav.GetAddressOf());
    const std::array<ID3D11ShaderResourceView*, 2> views = {ao_view.Get(), traced_view.Get()};
    context->CSSetShader(combine.Get(), nullptr, 0);
    context->CSSetConstantBuffers(0, 1, size_buffer.GetAddressOf());
    context->CSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
    context->CSSetSamplers(0, 1, sampler.GetAddressOf());
    context->CSSetUnorderedAccessViews(0, 1, uav.GetAddressOf(), nullptr);
    context->Dispatch((SIZE + 7) / 8, (SIZE + 7) / 8, 1);
    context->ClearState();
    return Read(output.Get());
  };
  const auto open = combined(1.f);
  const auto half = combined(0.5f);
  int alone = 0;
  int control = 0;
  for (size_t at = 0; at < traced.size(); ++at) {
    alone += (open[at][0] == traced[at]) ? 1 : 0;
    control += (traced[at] != 0.f && half[at][0] != traced[at]) ? 1 : 0;
  }
  const int texels = static_cast<int>(traced.size());
  CHECK(alone == texels, "the game's AO cleared to open: %d of %d combine to the traced visibility", alone, texels);
  CHECK(control > texels / 2, "CONTROL the game's AO 0.5: %d of %d differ", control, texels);
  // The battle's sun shaders (ATN's traces draw CF0C5118; 230F2241 part 1's): their light from the ambient cube alone.
  int red_only = 0;
  int follows = 0;
  for (const uint32_t hash : {0xCF0C5118u, 0x230F2241u}) {
    const auto code = GameBytes(dump, hash);
    SunScene scene = {.lighting = SunLighting({0.f, 0.f, 0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}, 1.f)};
    scene.ao = {0.25f, 0.f, 0.f, 0.f};
    const auto red_zero = DrawGame(code, scene);
    scene.ao = {0.25f, 1.f, 1.f, 1.f};
    const auto red_one = DrawGame(code, scene);
    scene.ao = {1.f, 0.f, 0.f, 0.f};
    const auto unoccluded = DrawGame(code, scene);
    red_only += (red_zero == red_one) ? 1 : 0;
    follows += (red_one != unoccluded && unoccluded[0] > 0.f) ? 1 : 0;
  }
  CHECK(red_only == 2, "the sun shaders read t_ssao's red only: %d of 2", red_only);
  CHECK(follows == 2, "CONTROL the sun shaders' light follows that red: %d of 2", follows);
  std::printf("rays' AO alone: the game's AO cleared to open, %d of %d combine to the traced visibility (CONTROL 0.5: %d differ); CF0C5118 / "
              "230F2241 read t_ssao's red only: %d of 2, their light follows it: %d of 2\n",
              alone, texels, control, red_only, follows);
}

// What the battle fog's draw binds besides the game's constants (part 16).
struct FogScene {
  float strength = 1.f;  // SI.fog_strength
};

// A battle fog pixel shader's bytes (`code`) drawn over 8 x 8 pixels, its constants by name as the game's bytes (`game`)
// name them: world = (4 x ndc x, 2 x ndc y, 16 x depth) (inv_view_projection's registers), depth 0.1 - 0.9 by pixel, the
// camera at (0, 1, -12); the fog 0 - 3 m high, its distance, clear distance (40 m) and forced fog (20 - 200 m) partial there;
// sun (0.3, -0.8, 0.5), colour (300, 280, 250), g_hdr_on 1; the sky cube's faces each their own colour.
static std::vector<Float4> DrawFog(std::span<const uint8_t> code, std::span<const uint8_t> game, const FogScene& scene) {
  ComPtr<ID3D11PixelShader> shader;
  if (FAILED(device->CreatePixelShader(code.data(), code.size(), nullptr, shader.GetAddressOf()))) return {};
  ComPtr<ID3D11ShaderReflection> reflection;
  if (FAILED(D3DReflect(game.data(), game.size(), IID_ID3D11ShaderReflection, reinterpret_cast<void**>(reflection.GetAddressOf())))) return {};
  const std::vector<NamedConstant> constants = {
      {"camera_position", 0, {0.f, 1.f, -12.f}},
      {"inv_view_projection", 0, {4.f, 0.f, 0.f, 0.f, 0.f, 2.f, 0.f, 0.f, 0.f, 0.f, 16.f, 0.f, 0.f, 0.f, 0.f, 1.f}},
      {"g_screen_size", 0, {8.f, 8.f, 0.125f, 0.125f}},
      {"g_vpos_texel_offset", 0, {0.f}},
      {"g_viewport_dimensions", 0, {8.f, 8.f, 0.125f, 0.125f}},
      {"g_viewport_origin", 0, {0.f, 0.f}},
      {"sun_direction", 0, {0.3f, -0.8f, 0.5f}},
      {"sun_colour", 0, {300.f, 280.f, 250.f}},
      {"g_hdr_on", 0, {1.f}},
      {"g_volume_fog_colour", 0, {0.5f, 0.6f, 0.7f}},
      {"g_fog_distance_start", 0, {0.4f}},
      {"g_fog_distance_strength", 0, {0.98f}},
      {"g_fog_distance_scale", 0, {0.5f}},
      {"g_fog_height_bottom", 0, {0.f}},
      {"g_fog_height_top", 0, {3.f}},
      {"g_fog_height_strength", 0, {0.25f}},
      {"g_fog_colour_blend", 0, {0.7f}},
      {"g_fog_clear_distance", 0, {40.f}},
      {"g_force_fog", 0, {20.f, 200.f}},
  };
  std::vector<ComPtr<ID3D11Buffer>> buffers;
  D3D11_SHADER_DESC desc = {};
  reflection->GetDesc(&desc);
  for (UINT index = 0; index < desc.ConstantBuffers; ++index) {
    auto* buffer = reflection->GetConstantBufferByIndex(index);
    D3D11_SHADER_BUFFER_DESC buffer_desc = {};
    buffer->GetDesc(&buffer_desc);
    D3D11_SHADER_INPUT_BIND_DESC bind = {};
    reflection->GetResourceBindingDescByName(buffer_desc.Name, &bind);
    std::vector<float> data(((buffer_desc.Size + 15) & ~15u) / 4, 0.f);
    for (UINT variable = 0; variable < buffer_desc.Variables; ++variable) {
      D3D11_SHADER_VARIABLE_DESC variable_desc = {};
      buffer->GetVariableByIndex(variable)->GetDesc(&variable_desc);
      for (const auto& constant : constants) {
        if (std::strcmp(constant.name, variable_desc.Name) == 0) {
          std::ranges::copy(constant.values, data.begin() + static_cast<ptrdiff_t>(variable_desc.StartOffset / 4));
        }
      }
    }
    buffers.push_back(Constants(data.data(), static_cast<UINT>(data.size() * 4)));
    context->PSSetConstantBuffers(bind.BindPoint, 1, buffers.back().GetAddressOf());
  }
  ShaderInjectData injection = {};
  injection.fog_strength = scene.strength;
  std::vector<uint8_t> injection_bytes((sizeof(injection) + 15) & ~size_t{15});
  std::memcpy(injection_bytes.data(), &injection, sizeof(injection));
  buffers.push_back(Constants(injection_bytes.data(), static_cast<UINT>(injection_bytes.size())));
  context->PSSetConstantBuffers(13, 1, buffers.back().GetAddressOf());
  std::vector<Float4> faces;
  for (UINT face = 0; face < 6; ++face) {
    faces.push_back({0.1f + (0.15f * static_cast<float>(face)), 0.9f - (0.1f * static_cast<float>(face)), 0.3f + (0.05f * static_cast<float>(face)), 1.f});
  }
  const auto cube = Texture(1, 1, faces.data(), D3D11_BIND_SHADER_RESOURCE, 6, D3D11_RESOURCE_MISC_TEXTURECUBE);
  std::vector<Float4> depths;
  for (UINT pixel = 0; pixel < 64; ++pixel) depths.push_back({0.1f + (0.8f * static_cast<float>((pixel * 37) % 64) / 63.f), 0.f, 0.f, 0.f});
  std::array<ComPtr<ID3D11ShaderResourceView>, 2> views;
  device->CreateShaderResourceView(cube.Get(), nullptr, views[0].GetAddressOf());
  views[1] = View(8, 8, depths);
  ComPtr<ID3D11SamplerState> sampler;
  const D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP, .MaxLOD = D3D11_FLOAT32_MAX};
  device->CreateSamplerState(&sampler_desc, sampler.GetAddressOf());
  for (UINT index = 0; index < desc.BoundResources; ++index) {
    D3D11_SHADER_INPUT_BIND_DESC bind = {};
    reflection->GetResourceBindingDesc(index, &bind);
    if (bind.Type == D3D_SIT_SAMPLER) context->PSSetSamplers(bind.BindPoint, 1, sampler.GetAddressOf());
    if (bind.Type == D3D_SIT_TEXTURE) {
      context->PSSetShaderResources(bind.BindPoint, 1, views[(bind.Dimension == D3D_SRV_DIMENSION_TEXTURECUBE) ? 0 : 1].GetAddressOf());
    }
  }
  const auto target = Texture(8, 8, nullptr, D3D11_BIND_RENDER_TARGET);
  ComPtr<ID3D11RenderTargetView> rtv;
  device->CreateRenderTargetView(target.Get(), nullptr, rtv.GetAddressOf());
  context->VSSetShader(full_screen.Get(), nullptr, 0);
  context->PSSetShader(shader.Get(), nullptr, 0);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D11_VIEWPORT viewport = {.Width = 8.f, .Height = 8.f, .MaxDepth = 1.f};
  context->RSSetViewports(1, &viewport);
  context->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
  context->Draw(3, 0);
  context->ClearState();
  return Read(target.Get());
}

static void TestFog(const std::string& root, const std::string& dump) {
  const auto game = GameBytes(dump, 0xE684D54E);
  const auto ours = Bytes(root + "/build/twwarhammer.include/embed/0xE684D54E.cso");
  const auto expected = DrawFog(game, game, {});
  const auto got = DrawFog(ours, game, {});
  const auto half = DrawFog(ours, game, {.strength = 0.5f});
  CHECK(expected.size() == 64 && got.size() == 64 && half.size() == 64, "fog: the draws");
  if (expected.size() != 64 || got.size() != 64 || half.size() != 64) return;
  int misses = 0;
  int partial = 0;
  int halved = 0;
  int control = 0;
  float largest = 0.f;
  for (size_t pixel = 0; pixel < 64; ++pixel) {
    for (size_t channel = 0; channel < 4; ++channel) {
      largest = std::max(largest, std::fabs(expected[pixel][channel] - got[pixel][channel]));
      if (!Near(expected[pixel][channel], got[pixel][channel], 2e-6f) && ++misses <= 4) {
        std::printf("  fog pixel %zu channel %zu: the game's %.9g, ours %.9g\n", pixel, channel, expected[pixel][channel], got[pixel][channel]);
      }
    }
    partial += (expected[pixel][3] > 0.05f && expected[pixel][3] < 0.95f) ? 1 : 0;
    halved += (Near(half[pixel][3], got[pixel][3] * 0.5f, 1e-6f) && half[pixel][0] == got[pixel][0] && half[pixel][1] == got[pixel][1] &&
               half[pixel][2] == got[pixel][2])
                  ? 1
                  : 0;
    control += Near(half[pixel][3], expected[pixel][3], 1e-3f) ? 0 : 1;
  }
  CHECK(misses == 0 && partial >= 32 && halved == 64 && control > 0, "fog: %d misses, %d partial, %d halved, CONTROL %d", misses, partial, halved,
        control);
  std::printf("fog: battlefog00 against E684D54E, 256 values, %d misses (largest %.3g), %d of 64 pixels partly fogged; Fog Strength 0.5: %d of 64 "
              "opacities halved, colour kept; CONTROL %d differ from the game's\n",
              misses, largest, partial, halved, control);
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("tests <repo root> <the game's shader dump folder>\n");
    return 2;
  }
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                               context.GetAddressOf()))) {
    return 2;
  }
  const auto vertex_code = CompileText("float4 main(uint id : SV_VertexID) : SV_Position { const float2 uv = float2((id << 1) & 2, id & 2);"
                                       " return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); }",
                                       "triangle", "main", "vs_5_0");
  device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, full_screen.GetAddressOf());
  const std::string games = std::string(argv[1]) + "/src/games";
  TestSun(games, argv[2]);
  TestSky(games, argv[2]);
  TestAmbientPatch(games, argv[2]);
  TestSharedBytes(games, argv[2]);
  TestResolve(games);
  TestCasters(argv[1], argv[2]);
  TestPointLights(argv[1], argv[2]);
  TestForwardShadows(argv[2]);
  TestGuides(games, argv[2]);
  TestThin(argv[2]);
  TestFoliage(argv[2]);
  TestWater(games, argv[2]);
  TestCutOuts(argv[1], argv[2]);
  TestReflectionInputs(games, argv[2]);
  TestAoAlone(games, argv[2]);
  TestFog(argv[1], argv[2]);
  std::printf(failures == 0 ? "PASS\n" : "FAILED: %d\n", failures);
  return failures == 0 ? 0 : 1;
}
