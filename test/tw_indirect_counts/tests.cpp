// Warhammer III's GPU-culled grass for the rays (twattila/rt_core.hpp CaptureIndirect, Settings::indirect_counts_later: its
// Grass Without Waiting row) on WARP, through the addon's own CaptureIndirect and Capture over a stand-in for the game's
// indirect draws: a compute pass writes each frame's numbers into a DRAWINDIRECT_ARGS buffer after GPU work that takes a
// while (a compute loop). Two draws: draw 0 always drawn (index 36 x instances 100 + the frame), draw 1 empty every third
// frame (else instances 1000 + the frame). Each draw's numbers reach Capture, whose "left out" line (a point-list topology
// bound) gives index x instances, so the frame they are from.
// 1. Row off (CONTROL, the waiting read): every capture has this frame's numbers; draw 1 none in its empty frames.
// 2. Row on: no capture has this frame's numbers, only an earlier frame's, never one of an empty frame, and no waiting read
//    happens (the read-back's frame stays unset).
// 3. The CPU time of CaptureIndirect over a frame's two draws, row off against row on, behind that GPU work.
// tests.exe [load dispatches]
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include "src/games/twattila/rt_scene.hpp"
#include "test/tw_particle_history/mock_reshade.hpp"

using Microsoft::WRL::ComPtr;
namespace scene = twattila::rt_scene;
namespace action = renodx::utils::command_action;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

extern "C" __declspec(dllexport) const char* const NAME = "Indirect counts test";
static std::mutex log_mutex;
static std::vector<std::string> log_lines;
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) {
  const std::unique_lock lock(log_mutex);
  log_lines.emplace_back(message);
}

static ComPtr<ID3DBlob> Compile(const char* source, const char* target) {
  ComPtr<ID3DBlob> blob;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompile(source, strlen(source), nullptr, nullptr, nullptr, "main", target, 0, 0, blob.GetAddressOf(), errors.GetAddressOf()))) {
    std::printf("compile: %s\n", static_cast<const char*>(errors->GetBufferPointer()));
    std::exit(2);
  }
  return blob;
}

constexpr uint32_t INDICES = 36;
constexpr uint32_t ARGUMENT_BYTES = 20;
constexpr std::array<uint32_t, 2> FIRST_INSTANCES = {100, 1000};  // instances = these + the frame's value

int main(int argc, char** argv) {
  const UINT load = (argc > 1) ? static_cast<UINT>(std::atoi(argv[1])) : 3u;
  mock::Device device;
  ComPtr<ID3D11DeviceContext> context;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.native.GetAddressOf(), nullptr,
                               context.GetAddressOf()))) {
    return 2;
  }
  mock::CommandList cmd_list;
  cmd_list.owner = &device;
  cmd_list.native = context;

  // The game's numbers, written on the GPU: index count, instances, first index, vertex offset, first instance per draw.
  const auto arguments_blob = Compile(
      "cbuffer c : register(b0) { uint4 frame; } RWBuffer<uint> a : register(u0);"
      "[numthreads(1, 1, 1)] void main() { a[0] = 36; a[1] = 100 + frame.x; a[2] = 0; a[3] = 0; a[4] = 0;"
      " a[5] = 36; a[6] = (frame.x % 3 == 0) ? 0 : 1000 + frame.x; a[7] = 0; a[8] = 0; a[9] = 0; }",
      "cs_5_0");
  ComPtr<ID3D11ComputeShader> arguments_shader;
  device.native->CreateComputeShader(arguments_blob->GetBufferPointer(), arguments_blob->GetBufferSize(), nullptr, arguments_shader.GetAddressOf());
  const D3D11_BUFFER_DESC arguments_desc = {.ByteWidth = ARGUMENT_BYTES * 2, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_UNORDERED_ACCESS,
                                            .MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS};
  ComPtr<ID3D11Buffer> arguments;
  CHECK(SUCCEEDED(device.native->CreateBuffer(&arguments_desc, nullptr, arguments.GetAddressOf())));
  const D3D11_UNORDERED_ACCESS_VIEW_DESC arguments_view_desc = {.Format = DXGI_FORMAT_R32_UINT, .ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
                                                                .Buffer = {.FirstElement = 0, .NumElements = 10}};
  ComPtr<ID3D11UnorderedAccessView> arguments_view;
  CHECK(SUCCEEDED(device.native->CreateUnorderedAccessView(arguments.Get(), &arguments_view_desc, arguments_view.GetAddressOf())));
  const D3D11_BUFFER_DESC constants_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  ComPtr<ID3D11Buffer> constants;
  device.native->CreateBuffer(&constants_desc, nullptr, constants.GetAddressOf());
  // The GPU's other work before the grass: a compute loop over a texture.
  const auto load_blob = Compile(
      "RWTexture2D<float4> t : register(u0); [numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) { float4 v = t[id.xy]; "
      "for (uint i = 0; i < 64; ++i) v = frac(v * 1.61803f + 0.1f); t[id.xy] = v; }",
      "cs_5_0");
  ComPtr<ID3D11ComputeShader> load_shader;
  device.native->CreateComputeShader(load_blob->GetBufferPointer(), load_blob->GetBufferSize(), nullptr, load_shader.GetAddressOf());
  const D3D11_TEXTURE2D_DESC load_desc = {.Width = 512, .Height = 512, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
                                          .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_UNORDERED_ACCESS};
  ComPtr<ID3D11Texture2D> load_texture;
  ComPtr<ID3D11UnorderedAccessView> load_view;
  device.native->CreateTexture2D(&load_desc, nullptr, load_texture.GetAddressOf());
  device.native->CreateUnorderedAccessView(load_texture.Get(), nullptr, load_view.GetAddressOf());
  // Capture's "left out" line names the numbers it got (a point list is no triangle list).
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);

  const scene::SceneShader shader = {.hash = 0xA4527F25, .dynamic = true, .indirect = true};
  auto& runtime = scene::internal::runtime;
  runtime.settings.sun_shadows = true;
  // A present: the frame latency DXGI keeps (3 frames queued at most), then rt_core Update's new frame.
  const D3D11_QUERY_DESC event_desc = {.Query = D3D11_QUERY_EVENT};
  std::array<ComPtr<ID3D11Query>, 3> frame_ends;
  for (auto& frame_end : frame_ends) device.native->CreateQuery(&event_desc, frame_end.GetAddressOf());
  uint64_t presented = 0;
  const auto present = [&] {
    ID3D11Query* frame_end = frame_ends[presented % frame_ends.size()].Get();
    if (presented >= frame_ends.size()) {
      BOOL done = FALSE;
      while (context->GetData(frame_end, &done, sizeof(done), 0) != S_OK) {
      }
    }
    context->End(frame_end);
    context->Flush();
    ++presented;
    const std::unique_lock lock(runtime.mutex);
    ++runtime.frame;
  };

  constexpr uint32_t FRAMES = 40;
  for (const bool later : {false, true}) {
    {
      const std::unique_lock lock(runtime.mutex);
      runtime.settings.indirect_counts_later = later;
      runtime.indirect_arguments_frame = 0;
      runtime.indirect_arguments_buffer = nullptr;
    }
    // Each pass's own frame values (the probe carries the last pass's numbers over).
    const uint32_t base = (later ? 200u : 0u);
    std::array<uint32_t, 2> this_frame = {};
    std::array<uint32_t, 2> earlier = {};
    std::array<uint32_t, 2> none = {};
    uint32_t wrong = 0;
    uint32_t most_late = 0;
    double capture_ms = 0.0;
    double capture_worst = 0.0;
    for (uint32_t frame = 1; frame <= FRAMES; ++frame) {
      const uint32_t value = base + frame;
      const std::array<uint32_t, 4> frame_constants = {value, 0, 0, 0};
      context->UpdateSubresource(constants.Get(), 0, nullptr, frame_constants.data(), 0, 0);
      context->CSSetShader(load_shader.Get(), nullptr, 0);
      context->CSSetUnorderedAccessViews(0, 1, load_view.GetAddressOf(), nullptr);
      for (UINT dispatch = 0; dispatch < load; ++dispatch) context->Dispatch(64, 64, 1);
      context->CSSetShader(arguments_shader.Get(), nullptr, 0);
      context->CSSetConstantBuffers(0, 1, constants.GetAddressOf());
      context->CSSetUnorderedAccessViews(0, 1, arguments_view.GetAddressOf(), nullptr);
      context->Dispatch(1, 1, 1);
      ID3D11UnorderedAccessView* no_view = nullptr;
      context->CSSetUnorderedAccessViews(0, 1, &no_view, nullptr);
      const auto started = std::chrono::steady_clock::now();
      std::array<uint32_t, 2> captured = {};  // index x instances each draw's capture got; 0: none
      for (uint32_t draw = 0; draw < captured.size(); ++draw) {
        {
          const std::unique_lock lock(runtime.mutex);
          runtime.left_out_logged.clear();
        }
        {
          const std::unique_lock lock(log_mutex);
          log_lines.clear();
        }
        action::CommandContext<action::IndirectArguments> command = {
            .cmd_list = &cmd_list,
            .arguments = {.command = reshade::api::indirect_command::draw_indexed, .buffer = {reinterpret_cast<uint64_t>(arguments.Get())},
                          .offset = draw * ARGUMENT_BYTES, .draw_count = 1, .stride = ARGUMENT_BYTES},
            .callback_data = &shader,
        };
        scene::internal::CaptureIndirect(command, nullptr);
        const std::unique_lock lock(log_mutex);
        for (const auto& line : log_lines) {
          if (const size_t at = line.find("topology 1, "); at != std::string::npos) {
            captured[draw] = static_cast<uint32_t>(std::atoi(line.c_str() + at + 12));
          }
        }
      }
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
      capture_ms += ms;
      capture_worst = std::max(capture_worst, ms);
      for (uint32_t draw = 0; draw < captured.size(); ++draw) {
        if (captured[draw] == 0) {
          ++none[draw];
          continue;
        }
        // A frame's value this test wrote: 1 to 40 (row off's pass), 201 to 240 (row on's: its first frames may read the
        // probe of row off's last ones).
        const uint32_t numbers_value = captured[draw] / INDICES - FIRST_INSTANCES[draw];
        const bool written = (numbers_value >= 1 && numbers_value <= FRAMES) || (numbers_value >= 201 && numbers_value <= 200 + FRAMES);
        if (captured[draw] % INDICES != 0 || !written || numbers_value > value || (draw == 1 && numbers_value % 3 == 0)) {
          ++wrong;
        } else if (numbers_value == value) {
          ++this_frame[draw];
        } else {
          ++earlier[draw];
          if (numbers_value > base) most_late = std::max(most_late, value - numbers_value);
        }
      }
      // Row off: draw 1's empty frames have no capture.
      if (!later && value % 3 == 0 && captured[1] != 0) ++wrong;
      present();
    }
    std::printf("row %s: draw 0 this frame's numbers %u, an earlier frame's %u, none %u; draw 1 this frame's %u, earlier %u, none %u; "
                "at most %u frames late; wrong %u; waiting read's frame %llu; CaptureIndirect %.3f ms a frame (worst %.3f)\n",
                later ? "on" : "off", this_frame[0], earlier[0], none[0], this_frame[1], earlier[1], none[1], most_late, wrong,
                static_cast<unsigned long long>(runtime.indirect_arguments_frame), capture_ms / FRAMES, capture_worst);
    CHECK(wrong == 0);
    if (later) {
      CHECK(this_frame[0] == 0 && this_frame[1] == 0 && earlier[0] >= FRAMES - 4 && runtime.indirect_arguments_frame == 0);
    } else {
      CHECK(this_frame[0] == FRAMES && earlier[0] == 0 && earlier[1] == 0 && runtime.indirect_arguments_frame != 0);
    }
  }
  std::printf("PASS\n");
  return 0;
}
