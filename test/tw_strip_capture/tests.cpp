// The rays' capture of a triangle strip (bug4_, DEBUG44 (e): the ships' rigging, rope.hlsl vs30_main_rigid 3926A823, drawn
// as a strip and left out by rt_core.hpp Capture). What Capture now counts on, on WARP's stream output:
// 1. A strip of N vertices, I instances, streamed out: I x (N - 2) triangles as a list, 3 x (N - 2) x I vertices (Capture's
//    vertex_count), each triangle the strip's (k, k + 1, k + 2), every odd one (k, k + 2, k + 1): the strip's winding kept.
// 2. An indexed strip cut by its strip-cut index (0xFFFF): fewer triangles, those of each piece; the capture's range past
//    them keeps the NaN it was emptied with (inactive triangles for the helper).
// CONTROL: the strip's indices read as a list (what a compact capture would make of them) are other triangles.
// tests.exe
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;
using Vertex = std::array<float, 4>;

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;
static int failures = 0;

static const char* PASS = R"(
struct Vertex { float4 position : POSITION; uint instance : SV_InstanceID; };
float4 main(Vertex input) : SV_Position { return float4(input.position.xyz, input.position.w + 100.0 * input.instance); }
)";

static void Check(const char* what, bool ok) {
  std::printf("  %s: %s\n", what, ok ? "ok" : "FAIL");
  failures += ok ? 0 : 1;
}

int main() {
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context))) return 1;
  ComPtr<ID3DBlob> blob, errors;
  if (FAILED(D3DCompile(PASS, std::strlen(PASS), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &blob, &errors))) return 1;
  ComPtr<ID3D11VertexShader> vertex_shader;
  device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &vertex_shader);
  const D3D11_SO_DECLARATION_ENTRY entry = {.SemanticName = "SV_Position", .StartComponent = 0, .ComponentCount = 4, .OutputSlot = 0};
  const UINT stride = 16;
  ComPtr<ID3D11GeometryShader> stream_output;
  if (FAILED(device->CreateGeometryShaderWithStreamOutput(blob->GetBufferPointer(), blob->GetBufferSize(), &entry, 1, &stride, 1,
                                                          D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &stream_output))) {
    return 1;
  }
  const D3D11_INPUT_ELEMENT_DESC element = {.SemanticName = "POSITION", .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .AlignedByteOffset = 0,
                                            .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA};
  ComPtr<ID3D11InputLayout> layout;
  device->CreateInputLayout(&element, 1, blob->GetBufferPointer(), blob->GetBufferSize(), &layout);
  // Vertex k at (k, 0, 0, k).
  std::vector<Vertex> vertices(12);
  for (size_t k = 0; k < vertices.size(); ++k) vertices[k] = {static_cast<float>(k), 0.f, 0.f, static_cast<float>(k)};
  const D3D11_BUFFER_DESC vertex_desc = {.ByteWidth = static_cast<UINT>(vertices.size() * 16), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_VERTEX_BUFFER};
  const D3D11_SUBRESOURCE_DATA vertex_init = {.pSysMem = vertices.data()};
  ComPtr<ID3D11Buffer> vertex_buffer;
  device->CreateBuffer(&vertex_desc, &vertex_init, &vertex_buffer);
  constexpr UINT CAPACITY = 64;
  const std::vector<uint32_t> empty(CAPACITY * 4, 0x7FC00000u);
  const D3D11_BUFFER_DESC captured_desc = {.ByteWidth = CAPACITY * 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_STREAM_OUTPUT};
  const D3D11_SUBRESOURCE_DATA empty_init = {.pSysMem = empty.data()};
  const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = CAPACITY * 16, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
  ComPtr<ID3D11Buffer> staging;
  device->CreateBuffer(&staging_desc, nullptr, &staging);

  // One capture: the range emptied (NaN), the draw streamed out; the vertices and the primitives written.
  const auto capture = [&](const std::vector<uint16_t>* indices, UINT count, UINT instances, uint64_t* written) {
    ComPtr<ID3D11Buffer> captured;
    device->CreateBuffer(&captured_desc, &empty_init, &captured);
    ComPtr<ID3D11Buffer> index_buffer;
    if (indices != nullptr) {
      const D3D11_BUFFER_DESC index_desc = {.ByteWidth = static_cast<UINT>(indices->size() * 2), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_INDEX_BUFFER};
      const D3D11_SUBRESOURCE_DATA index_init = {.pSysMem = indices->data()};
      device->CreateBuffer(&index_desc, &index_init, &index_buffer);
      context->IASetIndexBuffer(index_buffer.Get(), DXGI_FORMAT_R16_UINT, 0);
    }
    const UINT offset = 0;
    context->IASetInputLayout(layout.Get());
    context->IASetVertexBuffers(0, 1, vertex_buffer.GetAddressOf(), &stride, &offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context->VSSetShader(vertex_shader.Get(), nullptr, 0);
    context->GSSetShader(stream_output.Get(), nullptr, 0);
    context->SOSetTargets(1, captured.GetAddressOf(), &offset);
    const D3D11_QUERY_DESC query_desc = {.Query = D3D11_QUERY_SO_STATISTICS};
    ComPtr<ID3D11Query> query;
    device->CreateQuery(&query_desc, &query);
    context->Begin(query.Get());
    if (indices != nullptr) {
      context->DrawIndexedInstanced(count, instances, 0, 0, 0);
    } else {
      context->DrawInstanced(count, instances, 0, 0);
    }
    context->End(query.Get());
    D3D11_QUERY_DATA_SO_STATISTICS statistics = {};
    while (context->GetData(query.Get(), &statistics, sizeof(statistics), 0) == S_FALSE) {
    }
    *written = statistics.NumPrimitivesWritten;
    context->CopyResource(staging.Get(), captured.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    std::vector<Vertex> out(CAPACITY);
    std::memcpy(out.data(), mapped.pData, CAPACITY * 16);
    context->Unmap(staging.Get(), 0);
    context->ClearState();
    return out;
  };
  // Capture's vertex count for a strip (rt_core.hpp): 3 x (count - 2) an instance.
  const auto strip_vertices = [](UINT count, UINT instances) { return (count < 3) ? 0u : (count - 2) * 3 * instances; };
  // The triangles a strip of `strip` (vertex numbers) makes, as stream output writes them, for instance `instance`.
  const auto expected = [](const std::vector<uint32_t>& strip, UINT instance, std::vector<Vertex>* out) {
    for (size_t k = 0; k + 2 < strip.size(); ++k) {
      const std::array<uint32_t, 3> corners = (k % 2 == 0) ? std::array<uint32_t, 3>{strip[k], strip[k + 1], strip[k + 2]}
                                                           : std::array<uint32_t, 3>{strip[k], strip[k + 2], strip[k + 1]};
      for (const uint32_t corner : corners) out->push_back({static_cast<float>(corner), 0.f, 0.f, static_cast<float>(corner) + 100.f * instance});
    }
  };
  const auto same = [](const Vertex& a, const Vertex& b) { return std::memcmp(a.data(), b.data(), 16) == 0; };

  std::printf("1 a strip of 7 vertices, 2 instances (non-indexed)\n");
  {
    uint64_t primitives = 0;
    const auto out = capture(nullptr, 7, 2, &primitives);
    const UINT count = strip_vertices(7, 2);
    std::vector<Vertex> want;
    for (UINT instance = 0; instance < 2; ++instance) expected({0, 1, 2, 3, 4, 5, 6}, instance, &want);
    bool match = want.size() == count;
    for (UINT index = 0; index < count && match; ++index) match = same(out[index], want[index]);
    std::printf("  %llu triangles written, Capture's count %u vertices\n  streamed:", static_cast<unsigned long long>(primitives), count);
    for (UINT index = 0; index < 12; ++index) std::printf(" %g", out[index][0]);
    std::printf("\n  expected:");
    for (UINT index = 0; index < 12; ++index) std::printf(" %g", want[index][0]);
    std::printf("\n");
    Check("3 x (N - 2) x I vertices", primitives * 3 == count);
    Check("the strip's triangles, its winding kept", match);
    Check("past them the range's NaN", std::isnan(out[count][0]));
  }
  std::printf("2 an indexed strip of 11 indices cut once (0..4, cut, 5..9)\n");
  {
    const std::vector<uint16_t> indices = {0, 1, 2, 3, 4, 0xFFFF, 5, 6, 7, 8, 9};
    uint64_t primitives = 0;
    const auto out = capture(&indices, static_cast<UINT>(indices.size()), 1, &primitives);
    const UINT count = strip_vertices(static_cast<UINT>(indices.size()), 1);
    std::vector<Vertex> want;
    expected({0, 1, 2, 3, 4}, 0, &want);
    expected({5, 6, 7, 8, 9}, 0, &want);
    bool match = primitives * 3 == want.size();
    for (size_t index = 0; index < want.size() && match; ++index) match = same(out[index], want[index]);
    bool empty_rest = true;
    for (UINT index = static_cast<UINT>(want.size()); index < count; ++index) empty_rest = empty_rest && std::isnan(out[index][0]) && std::isnan(out[index][3]);
    std::printf("  %llu triangles written, Capture's range %u vertices\n", static_cast<unsigned long long>(primitives), count);
    Check("each piece's triangles", match);
    Check("fewer than the range: the rest still NaN (inactive)", primitives * 3 < count && empty_rest);
    // CONTROL: its indices read as a list.
    bool list_same = true;
    for (size_t index = 0; index + 3 <= 9 && list_same; ++index) list_same = indices[index] == static_cast<uint16_t>(want[index][0]);
    Check("CONTROL the indices as a list are other triangles", !list_same);
  }
  std::printf(failures == 0 ? "STRIP CAPTURE ALL PASSED\n" : "STRIP CAPTURE: %d FAILED\n", failures);
  return failures == 0 ? 0 : 1;
}
