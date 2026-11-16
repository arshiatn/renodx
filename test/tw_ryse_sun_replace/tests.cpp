// Ryse's Rays Replace Shadow Maps (rysesonofrome/contact_shadows.hpp) on WARP, with the add-on's built csos: the light
// list's sun found by 0x52C5E001, then contact_shadows::Apply's draws into the shadow mask array (RGBA8, two slices,
// every channel holding the game's values): rt_sun_keep (x 0 in the sun's channels, x 1 elsewhere) and 0x52C5E003 (the
// rays' shadow, max) with the contact march off as under the rays. Checked, for the sun in slice 0's G, A, R + B:
// the sun's channels hold the rays' shadow alone, bit for bit what the max pass writes onto a cleared mask; every other
// channel, slice 1 and the sky keep the game's bytes bit for bit. The sun in slice 1, or none: nothing changes.
// CONTROL: the max pass alone (the row off) keeps the game's shadow where it is darker than the rays' (must differ).
// Argument: the repository (build\rysesonofrome.include\embed); only read.
#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

int failures = 0;

#define CHECK(condition)                                       \
  do {                                                         \
    if (!(condition)) {                                        \
      std::printf("FAIL line %d: %s\n", __LINE__, #condition); \
      ++failures;                                              \
    }                                                          \
  } while (false)

std::vector<char> ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

constexpr UINT EDGE = 64;
constexpr UINT SKY_ROWS = 8;  // depth 1 there (the sky: 0x52C5E003 and rt_sun_keep leave it)

uint32_t seed = 4242;
uint32_t Random() {
  seed = seed * 1664525u + 1013904223u;
  return seed >> 8;
}

// 0x52C5E001's TiledLightShadeInfo: 256 bytes, light_type at 0, shadow_mask_index at 8, shadow_channel at 240.
struct Light {
  uint32_t light_type = 0;
  uint32_t resource_index = 0;
  uint32_t shadow_mask_index = 0;
  uint32_t padding = 0;
  std::array<float, 56> middle = {};
  std::array<float, 4> shadow_channel = {};
};
static_assert(sizeof(Light) == 256);

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: ryse_sun_replace.exe <repository>\n");
    return 2;
  }
  const std::filesystem::path embed = std::filesystem::path(argv[1]) / "build/rysesonofrome.include/embed";
  const auto find_code = ReadFile(embed / "0x52C5E001.cso");
  const auto vertex_code = ReadFile(embed / "0x52C5E002.cso");
  const auto pass_code = ReadFile(embed / "0x52C5E003.cso");
  const auto keep_code = ReadFile(embed / "rt_sun_keep.cso");
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                               context.GetAddressOf()))) {
    std::printf("no WARP device\n");
    return 2;
  }
  ComPtr<ID3D11ComputeShader> find_sun;
  ComPtr<ID3D11VertexShader> vertex_shader;
  ComPtr<ID3D11PixelShader> pass;
  ComPtr<ID3D11PixelShader> keep;
  if (find_code.empty() || vertex_code.empty() || pass_code.empty() || keep_code.empty()
      || FAILED(device->CreateComputeShader(find_code.data(), find_code.size(), nullptr, find_sun.GetAddressOf()))
      || FAILED(device->CreateVertexShader(vertex_code.data(), vertex_code.size(), nullptr, vertex_shader.GetAddressOf()))
      || FAILED(device->CreatePixelShader(pass_code.data(), pass_code.size(), nullptr, pass.GetAddressOf()))
      || FAILED(device->CreatePixelShader(keep_code.data(), keep_code.size(), nullptr, keep.GetAddressOf()))) {
    std::printf("the built csos are missing or not made (build the add-on first)\n");
    return 2;
  }

  // contact_shadows.hpp's blends: the mask max (target 0), the keep pass's multiply.
  D3D11_BLEND_DESC max_desc = {.IndependentBlendEnable = TRUE};
  max_desc.RenderTarget[0] = {
      .BlendEnable = TRUE,
      .SrcBlend = D3D11_BLEND_ONE,
      .DestBlend = D3D11_BLEND_ONE,
      .BlendOp = D3D11_BLEND_OP_MAX,
      .SrcBlendAlpha = D3D11_BLEND_ONE,
      .DestBlendAlpha = D3D11_BLEND_ONE,
      .BlendOpAlpha = D3D11_BLEND_OP_MAX,
      .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL,
  };
  max_desc.RenderTarget[1] = max_desc.RenderTarget[0];
  max_desc.RenderTarget[1].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALPHA;
  D3D11_BLEND_DESC keep_desc = {};
  keep_desc.RenderTarget[0] = {
      .BlendEnable = TRUE,
      .SrcBlend = D3D11_BLEND_ZERO,
      .DestBlend = D3D11_BLEND_SRC_COLOR,
      .BlendOp = D3D11_BLEND_OP_ADD,
      .SrcBlendAlpha = D3D11_BLEND_ZERO,
      .DestBlendAlpha = D3D11_BLEND_SRC_ALPHA,
      .BlendOpAlpha = D3D11_BLEND_OP_ADD,
      .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL,
  };
  const D3D11_RASTERIZER_DESC no_cull_desc = {.FillMode = D3D11_FILL_SOLID, .CullMode = D3D11_CULL_NONE, .DepthClipEnable = TRUE};
  ComPtr<ID3D11BlendState> max_blend;
  ComPtr<ID3D11BlendState> keep_blend;
  ComPtr<ID3D11RasterizerState> no_cull;
  device->CreateBlendState(&max_desc, max_blend.GetAddressOf());
  device->CreateBlendState(&keep_desc, keep_blend.GetAddressOf());
  device->CreateRasterizerState(&no_cull_desc, no_cull.GetAddressOf());

  // The scene: depth 0.5 below the sky rows; the rays' visibility random, fully lit on the left quarter.
  std::vector<float> depth(EDGE * EDGE);
  std::vector<float> visibility(EDGE * EDGE);
  for (UINT texel = 0; texel < EDGE * EDGE; ++texel) {
    depth[texel] = (texel / EDGE < SKY_ROWS) ? 1.f : 0.5f;
    visibility[texel] = (texel % EDGE < EDGE / 4) ? 1.f : static_cast<float>(Random() % 1001) / 1000.f;
  }
  const auto float_texture = [&](const std::vector<float>& texels) {
    const D3D11_TEXTURE2D_DESC desc = {.Width = EDGE,
                                       .Height = EDGE,
                                       .MipLevels = 1,
                                       .ArraySize = 1,
                                       .Format = DXGI_FORMAT_R32_FLOAT,
                                       .SampleDesc = {.Count = 1},
                                       .BindFlags = D3D11_BIND_SHADER_RESOURCE};
    const D3D11_SUBRESOURCE_DATA data = {.pSysMem = texels.data(), .SysMemPitch = EDGE * 4};
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    device->CreateTexture2D(&desc, &data, texture.GetAddressOf());
    device->CreateShaderResourceView(texture.Get(), nullptr, srv.GetAddressOf());
    return srv;
  };
  const auto depth_srv = float_texture(depth);
  const auto visibility_srv = float_texture(visibility);
  // PER_BATCH: a camera looking down +z (the march is off; only the sky test and the sun's direction are read).
  const std::array<float, 32> batch = {-1.f, 1.f, 100.f, 0.f, -1.f, -1.f, 100.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.1f, 100.f, 0.f, 0.f,
                                       0.3f, 0.8f, 0.2f, 0.f, 1.f, 1.f, 1.f, 0.f, 1.f, 1.f, 100.f, 0.f, EDGE, EDGE, 1.f / EDGE, 1.f / EDGE};
  const std::array<float, 4> contact = {0.5f, 0.2f, 0.f, 0.f};  // length, thickness, strength 0 (under the rays), frame
  const auto constants = [&](const void* values, UINT bytes) {
    const D3D11_BUFFER_DESC desc = {.ByteWidth = bytes, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_SUBRESOURCE_DATA data = {.pSysMem = values};
    ComPtr<ID3D11Buffer> buffer;
    device->CreateBuffer(&desc, &data, buffer.GetAddressOf());
    return buffer;
  };
  const auto batch_buffer = constants(batch.data(), sizeof(batch));
  const auto contact_buffer = constants(contact.data(), sizeof(contact));
  // The game's mask: random bytes in both slices.
  std::vector<uint32_t> game_mask(EDGE * EDGE * 2);
  for (auto& texel : game_mask) texel = Random();
  const D3D11_BUFFER_DESC sun_desc = {.ByteWidth = 8, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS};
  ComPtr<ID3D11Buffer> sun;
  device->CreateBuffer(&sun_desc, nullptr, sun.GetAddressOf());
  D3D11_UNORDERED_ACCESS_VIEW_DESC sun_uav_desc = {.Format = DXGI_FORMAT_R32_UINT, .ViewDimension = D3D11_UAV_DIMENSION_BUFFER};
  sun_uav_desc.Buffer.NumElements = 2;
  D3D11_SHADER_RESOURCE_VIEW_DESC sun_srv_desc = {.Format = DXGI_FORMAT_R32_UINT, .ViewDimension = D3D11_SRV_DIMENSION_BUFFER};
  sun_srv_desc.Buffer.NumElements = 2;
  ComPtr<ID3D11UnorderedAccessView> sun_uav;
  ComPtr<ID3D11ShaderResourceView> sun_srv;
  device->CreateUnorderedAccessView(sun.Get(), &sun_uav_desc, sun_uav.GetAddressOf());
  device->CreateShaderResourceView(sun.Get(), &sun_srv_desc, sun_srv.GetAddressOf());

  // One frame: the light list (a probe, then the sun at `slice` with `channel` weights; `with_sun` false: probes only),
  // 0x52C5E001, then Apply's draws onto the mask (`cleared`: a mask of zeros in place of the game's). Both slices' texels.
  const auto frame = [&](uint32_t slice, const std::array<float, 4>& channel, bool with_sun, bool replace, bool cleared) {
    std::array<Light, 4> lights = {};
    lights[0].light_type = 1;
    lights[1].light_type = with_sun ? 8u : 1u;
    lights[1].shadow_mask_index = slice;
    lights[1].shadow_channel = channel;
    const D3D11_BUFFER_DESC list_desc = {.ByteWidth = sizeof(lights),
                                         .Usage = D3D11_USAGE_DEFAULT,
                                         .BindFlags = D3D11_BIND_SHADER_RESOURCE,
                                         .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
                                         .StructureByteStride = sizeof(Light)};
    const D3D11_SUBRESOURCE_DATA list_data = {.pSysMem = lights.data()};
    ComPtr<ID3D11Buffer> list;
    ComPtr<ID3D11ShaderResourceView> list_srv;
    device->CreateBuffer(&list_desc, &list_data, list.GetAddressOf());
    device->CreateShaderResourceView(list.Get(), nullptr, list_srv.GetAddressOf());
    const D3D11_TEXTURE2D_DESC mask_desc = {.Width = EDGE,
                                            .Height = EDGE,
                                            .MipLevels = 1,
                                            .ArraySize = 2,
                                            .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                                            .SampleDesc = {.Count = 1},
                                            .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET};
    const std::vector<uint32_t> zeros(game_mask.size(), 0u);
    const std::array<D3D11_SUBRESOURCE_DATA, 2> mask_data = {{{.pSysMem = (cleared ? zeros : game_mask).data(), .SysMemPitch = EDGE * 4},
                                                              {.pSysMem = (cleared ? zeros : game_mask).data() + (EDGE * EDGE), .SysMemPitch = EDGE * 4}}};
    ComPtr<ID3D11Texture2D> mask;
    device->CreateTexture2D(&mask_desc, mask_data.data(), mask.GetAddressOf());
    // contact_shadows.hpp's target: slice 0 as a one-slice array.
    D3D11_RENDER_TARGET_VIEW_DESC target_desc = {.Format = DXGI_FORMAT_R8G8B8A8_UNORM, .ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY};
    target_desc.Texture2DArray = {.MipSlice = 0, .FirstArraySlice = 0, .ArraySize = 1};
    ComPtr<ID3D11RenderTargetView> target;
    device->CreateRenderTargetView(mask.Get(), &target_desc, target.GetAddressOf());

    context->ClearState();
    ID3D11ShaderResourceView* list_view = list_srv.Get();
    ID3D11UnorderedAccessView* sun_output = sun_uav.Get();
    context->CSSetShader(find_sun.Get(), nullptr, 0);
    context->CSSetShaderResources(0, 1, &list_view);
    context->CSSetUnorderedAccessViews(0, 1, &sun_output, nullptr);
    context->Dispatch(1, 1, 1);
    ID3D11UnorderedAccessView* no_output = nullptr;
    context->CSSetUnorderedAccessViews(0, 1, &no_output, nullptr);
    const D3D11_VIEWPORT viewport = {.Width = EDGE, .Height = EDGE, .MaxDepth = 1.f};
    ID3D11ShaderResourceView* inputs[] = {depth_srv.Get(), sun_srv.Get(), visibility_srv.Get(), nullptr};
    ID3D11Buffer* buffers[] = {batch_buffer.Get(), contact_buffer.Get()};
    ID3D11RenderTargetView* targets[] = {target.Get(), nullptr};
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertex_shader.Get(), nullptr, 0);
    context->PSSetShaderResources(0, 4, inputs);
    context->PSSetConstantBuffers(0, 2, buffers);
    context->RSSetState(no_cull.Get());
    context->RSSetViewports(1, &viewport);
    if (replace) {
      context->PSSetShader(keep.Get(), nullptr, 0);
      context->OMSetBlendState(keep_blend.Get(), nullptr, 0xFFFFFFFF);
      context->OMSetRenderTargets(1, targets, nullptr);
      context->Draw(3, 0);
    }
    context->PSSetShader(pass.Get(), nullptr, 0);
    context->OMSetBlendState(max_blend.Get(), nullptr, 0xFFFFFFFF);
    context->OMSetRenderTargets(2, targets, nullptr);
    context->Draw(3, 0);
    context->OMSetRenderTargets(0, nullptr, nullptr);

    D3D11_TEXTURE2D_DESC staging_desc = mask_desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    device->CreateTexture2D(&staging_desc, nullptr, staging.GetAddressOf());
    context->CopyResource(staging.Get(), mask.Get());
    std::vector<uint32_t> texels(game_mask.size());
    for (UINT index = 0; index < 2; ++index) {
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      if (FAILED(context->Map(staging.Get(), index, D3D11_MAP_READ, 0, &mapped))) continue;
      for (UINT y = 0; y < EDGE; ++y) {
        std::copy_n(reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(mapped.pData) + (y * mapped.RowPitch)), EDGE,
                    texels.begin() + (((index * EDGE) + y) * EDGE));
      }
      context->Unmap(staging.Get(), index);
    }
    return texels;
  };

  // Per case: texels where the sun's channels differ from the rays' alone, where any other byte differs from the game's.
  struct Case {
    const char* name;
    std::array<float, 4> channel;
    uint32_t bits;
  };
  const std::array<Case, 3> cases = {{{"G", {0.f, 1.f, 0.f, 0.f}, 0x0000FF00u}, {"A", {0.f, 0.f, 0.f, 1.f}, 0xFF000000u},
                                      {"R + B", {1.f, 0.f, 1.f, 0.f}, 0x00FF00FFu}}};
  for (const auto& sun_case : cases) {
    const auto rays_alone = frame(0, sun_case.channel, true, false, true);
    const auto replaced = frame(0, sun_case.channel, true, true, false);
    const auto darker = frame(0, sun_case.channel, true, false, false);
    size_t sun_off = 0;
    size_t kept_off = 0;
    size_t control_differs = 0;
    size_t shadowed = 0;
    for (size_t texel = 0; texel < game_mask.size(); ++texel) {
      const bool sun_texel = texel < EDGE * EDGE && texel / EDGE >= SKY_ROWS;
      const uint32_t bits = sun_texel ? sun_case.bits : 0u;
      if ((replaced[texel] & bits) != (rays_alone[texel] & bits)) ++sun_off;
      if ((replaced[texel] & ~bits) != (game_mask[texel] & ~bits)) ++kept_off;
      if ((darker[texel] & bits) != (rays_alone[texel] & bits)) ++control_differs;
      if ((rays_alone[texel] & bits) != 0u) ++shadowed;
    }
    std::printf("sun in slice 0 %s: the sun's channels %zu of %u texels off the rays' alone (%zu texels shadowed by them), other "
                "bytes / slice 1 / sky %zu off the game's; CONTROL (max alone) %zu texels keep the game's darker shadow\n",
                sun_case.name, sun_off, (EDGE - SKY_ROWS) * EDGE, shadowed, kept_off, control_differs);
    CHECK(sun_off == 0);
    CHECK(kept_off == 0);
    CHECK(shadowed > 1000);
    CHECK(control_differs > 1000);
  }
  // The sun in slice 1 (its target isn't bound), or no sun: the mask untouched.
  for (const bool with_sun : {true, false}) {
    const auto untouched = frame(1, {0.f, 1.f, 0.f, 0.f}, with_sun, true, false);
    size_t off = 0;
    for (size_t texel = 0; texel < game_mask.size(); ++texel) off += (untouched[texel] != game_mask[texel]) ? 1u : 0u;
    std::printf("%s: %zu texels off the game's mask\n", with_sun ? "sun in slice 1" : "no sun", off);
    CHECK(off == 0);
  }
  std::printf("%s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
  return failures == 0 ? 0 : 1;
}
