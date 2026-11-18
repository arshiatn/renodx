// The map trace (twattila/frame_trace.hpp, env RENODX_TW_MAP_TRACE=<seconds after the first present>[,<frames>]) on WARP
// with ReShade's event lists mocked: the test makes the D3D11 calls and then calls the registered callbacks as ReShade's
// D3D11 layer does (a map's after Map returned, an unmap's before Unmap, a copy's before the copy).
// 1. Off (no variable): nothing registered, no file. Malformed variables (junk after the delay or the frame count, a
//    frame count outside 1-100, a negative delay): off too, nothing registered.
// 2. RENODX_TW_MAP_TRACE=0.15,2 under a comma-decimal C locale (the parse ignores it): the callbacks registered at
//    Install, gated off: a map before the trace's start is not in the file; the first present after 0.15 s starts two
//    frames; after them the callbacks stay registered (until Uninstall) and record nothing. tw_map_trace.txt next to
//    the exe: the header, the module, the presents (the two before the trace, then its three), two frame lines (their
//    counts); frame 0: three indexed draws, a dynamic buffer's DISCARD map (draw index 3, the last draw's shaders, its
//    gap from the draw, its unmap) and a NO_OVERWRITE map 20 ms later (its gap >= 20 ms from the unmap, the trace's own
//    time left out), a default buffer's update, a dispatch; frame 1: a copy into a staging texture (source and
//    destination described), a copy into a default texture (not traced), a whole-buffer copy into a staging buffer and
//    a whole-buffer update (ReShade's UINT64_MAX: data_bytes=0), the staging texture's READ map, a texture map never
//    unmapped, an unmap of a buffer mapped before the trace, a map whose frame lies below the TEB's stack (its low limit
//    moved above this frame: a job system's own stack: no scan, callers=-). Every other map's first callers in the exe's
//    code, each once.
// 3. waits.txt for tools\tw\map_trace.py's join (run.ps1): the presents on another clock (QPC microseconds minus an
//    offset) and two waits on this thread, one inside the NO_OVERWRITE map's gap, one between events.
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <clocale>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "src/games/twattila/frame_trace.hpp"
#include "test/tw_particle_history/mock_reshade.hpp"

using Microsoft::WRL::ComPtr;
using api_event = reshade::addon_event;
namespace frame_trace = twattila::frame_trace;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

// ---- ReShade: the exports the utilities call and the event lists.
static std::array<std::vector<void*>, 512> event_lists;
extern "C" __declspec(dllexport) const char* const NAME = "Map trace test";
extern "C" {
__declspec(dllexport) bool ReShadeRegisterAddon(void*, uint32_t) { return true; }
__declspec(dllexport) void ReShadeUnregisterAddon(void*) {}
__declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) { std::printf("  log: %s\n", message); }
__declspec(dllexport) bool ReShadeGetConfigValue(void*, void*, const char*, const char*, char*, size_t*) { return false; }
__declspec(dllexport) void ReShadeSetConfigValue(void*, void*, const char*, const char*, const char*) {}
__declspec(dllexport) void ReShadeRegisterEvent(api_event ev, void* callback) { event_lists[static_cast<size_t>(ev)].push_back(callback); }
__declspec(dllexport) void ReShadeRegisterEventForAddon(void*, api_event ev, void* callback) { event_lists[static_cast<size_t>(ev)].push_back(callback); }
__declspec(dllexport) void ReShadeUnregisterEvent(api_event ev, void* callback) { std::erase(event_lists[static_cast<size_t>(ev)], callback); }
__declspec(dllexport) void ReShadeUnregisterEventForAddon(void*, api_event ev, void* callback) { std::erase(event_lists[static_cast<size_t>(ev)], callback); }
}

template <api_event ev>
using Decl = typename reshade::addon_event_traits<ev>::decl;

template <api_event ev, typename... Args>
void Invoke(Args... args) {
  const auto list = event_lists[static_cast<size_t>(ev)];
  for (void* callback : list) reinterpret_cast<Decl<ev>>(callback)(args...);
}

// No tracked state: every shader hash 0.
struct TestList : mock::CommandList {
  void get_private_data(const uint8_t[16], uint64_t* data) const override { *data = 0; }
  void set_private_data(const uint8_t[16], const uint64_t) override {}
};

static size_t Registered() {
  size_t count = 0;
  for (const auto ev : {api_event::draw, api_event::draw_indexed, api_event::dispatch, api_event::map_buffer_region, api_event::map_texture_region,
                        api_event::unmap_buffer_region, api_event::unmap_texture_region, api_event::update_buffer_region, api_event::copy_resource,
                        api_event::copy_buffer_region, api_event::copy_texture_region, api_event::draw_or_dispatch_indirect}) {
    count += event_lists[static_cast<size_t>(ev)].size();
  }
  return count;
}

static reshade::api::resource Handle(ID3D11Resource* resource) { return {reinterpret_cast<uint64_t>(resource)}; }

static std::vector<std::string> Lines(const std::filesystem::path& path, std::string_view kind) {
  std::vector<std::string> lines;
  std::ifstream file(path);
  for (std::string line; std::getline(file, line);) {
    if (line.starts_with(std::string(kind) + " ")) lines.push_back(line);
  }
  return lines;
}

static std::string Field(const std::string& line, std::string_view key) {
  std::istringstream tokens(line);
  for (std::string token; tokens >> token;) {
    if (token.starts_with(std::string(key) + "=")) return token.substr(key.size() + 1);
  }
  return "";
}

static int64_t Qpc() {
  LARGE_INTEGER now = {};
  QueryPerformanceCounter(&now);
  return now.QuadPart;
}

int main() {
  wchar_t exe_path[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
  const std::filesystem::path folder = std::filesystem::path(exe_path).parent_path();
  const std::string exe_name = std::filesystem::path(exe_path).filename().string();
  const auto trace_path = folder / L"tw_map_trace.txt";
  std::filesystem::remove(trace_path);

  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                                    context.GetAddressOf())));
  mock::Device mock_device;
  mock_device.native = device;
  TestList list;
  list.native = context;
  list.owner = &mock_device;

  // 1. Off.
  SetEnvironmentVariableW(L"RENODX_TW_MAP_TRACE", nullptr);
  frame_trace::Install();
  for (int present = 0; present < 5; ++present) frame_trace::OnPresent(false);
  CHECK(Registered() == 0);
  frame_trace::Uninstall();
  for (const wchar_t* malformed : {L"soon", L"95abc", L"95;3", L"95,x", L"95,-1", L"95,0", L"95,101", L"95,3x", L"-1", L"1e999"}) {
    SetEnvironmentVariableW(L"RENODX_TW_MAP_TRACE", malformed);
    frame_trace::Install();
    frame_trace::OnPresent(false);
    CHECK(Registered() == 0);
    frame_trace::Uninstall();
  }
  CHECK(!std::filesystem::exists(trace_path));
  std::printf("1. off and 10 malformed settings: nothing registered, no file\n");

  // 2. Two frames from 0.15 s after the first present.
  const bool comma_locale = std::setlocale(LC_ALL, "de-DE") != nullptr;
  SetEnvironmentVariableW(L"RENODX_TW_MAP_TRACE", L"0.15,2");
  frame_trace::Install();
  std::setlocale(LC_ALL, "C");
  CHECK(Registered() == 12);
  frame_trace::OnPresent(false);
  frame_trace::OnPresent(false);
  const D3D11_BUFFER_DESC before_desc = {.ByteWidth = 512, .Usage = D3D11_USAGE_DYNAMIC, .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
                                         .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE};
  ComPtr<ID3D11Buffer> before_buffer;
  CHECK(SUCCEEDED(device->CreateBuffer(&before_desc, nullptr, before_buffer.GetAddressOf())));
  Invoke<api_event::map_buffer_region>(static_cast<reshade::api::device*>(&mock_device), Handle(before_buffer.Get()), uint64_t{0}, UINT64_MAX,
                                       reshade::api::map_access::write_discard, static_cast<void**>(nullptr));
  Invoke<api_event::draw>(static_cast<reshade::api::command_list*>(&list), 3u, 1u, 0u, 0u);
  Sleep(200);
  LARGE_INTEGER frequency = {};
  QueryPerformanceFrequency(&frequency);
  std::vector<int64_t> presents = {Qpc()};
  frame_trace::OnPresent(false);
  CHECK(Registered() == 12);
  CHECK(frame_trace::internal::map_trace.tracing.load());

  const D3D11_BUFFER_DESC dynamic_desc = {.ByteWidth = 256, .Usage = D3D11_USAGE_DYNAMIC, .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
                                          .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE};
  ComPtr<ID3D11Buffer> dynamic_buffer;
  CHECK(SUCCEEDED(device->CreateBuffer(&dynamic_desc, nullptr, dynamic_buffer.GetAddressOf())));
  const D3D11_BUFFER_DESC default_desc = {.ByteWidth = 1024, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_VERTEX_BUFFER};
  ComPtr<ID3D11Buffer> default_buffer;
  CHECK(SUCCEEDED(device->CreateBuffer(&default_desc, nullptr, default_buffer.GetAddressOf())));

  // Frame 0.
  for (int draw = 0; draw < 3; ++draw) Invoke<api_event::draw_indexed>(static_cast<reshade::api::command_list*>(&list), 36u, 1u, 0u, 0, 0u);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  CHECK(SUCCEEDED(context->Map(dynamic_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
  Invoke<api_event::map_buffer_region>(static_cast<reshade::api::device*>(&mock_device), Handle(dynamic_buffer.Get()), uint64_t{0}, UINT64_MAX,
                                       reshade::api::map_access::write_discard, &mapped.pData);
  Invoke<api_event::unmap_buffer_region>(static_cast<reshade::api::device*>(&mock_device), Handle(dynamic_buffer.Get()));
  context->Unmap(dynamic_buffer.Get(), 0);
  const int64_t wait_start = Qpc();
  Sleep(20);
  CHECK(SUCCEEDED(context->Map(dynamic_buffer.Get(), 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped)));
  const int64_t wait_end = Qpc();
  Invoke<api_event::map_buffer_region>(static_cast<reshade::api::device*>(&mock_device), Handle(dynamic_buffer.Get()), uint64_t{0}, UINT64_MAX,
                                       reshade::api::map_access::write_only, &mapped.pData);
  Invoke<api_event::unmap_buffer_region>(static_cast<reshade::api::device*>(&mock_device), Handle(dynamic_buffer.Get()));
  context->Unmap(dynamic_buffer.Get(), 0);
  const std::array<uint8_t, 64> bytes = {};
  Invoke<api_event::update_buffer_region>(static_cast<reshade::api::device*>(&mock_device), static_cast<const void*>(bytes.data()),
                                          Handle(default_buffer.Get()), uint64_t{0}, uint64_t{64});
  context->UpdateSubresource(default_buffer.Get(), 0, nullptr, bytes.data(), 0, 0);
  Invoke<api_event::dispatch>(static_cast<reshade::api::command_list*>(&list), 8u, 8u, 1u);
  presents.push_back(Qpc());
  frame_trace::OnPresent(false);
  CHECK(Registered() == 12);

  // Frame 1.
  D3D11_TEXTURE2D_DESC texture_desc = {.Width = 64, .Height = 32, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                                       .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  ComPtr<ID3D11Texture2D> texture;
  CHECK(SUCCEEDED(device->CreateTexture2D(&texture_desc, nullptr, texture.GetAddressOf())));
  ComPtr<ID3D11Texture2D> texture_copy;
  CHECK(SUCCEEDED(device->CreateTexture2D(&texture_desc, nullptr, texture_copy.GetAddressOf())));
  texture_desc = {.Width = 64, .Height = 32, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R8G8B8A8_UNORM, .SampleDesc = {.Count = 1},
                  .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
  ComPtr<ID3D11Texture2D> staging;
  CHECK(SUCCEEDED(device->CreateTexture2D(&texture_desc, nullptr, staging.GetAddressOf())));
  Invoke<api_event::copy_resource>(static_cast<reshade::api::command_list*>(&list), Handle(texture.Get()), Handle(staging.Get()));
  context->CopyResource(staging.Get(), texture.Get());
  Invoke<api_event::copy_resource>(static_cast<reshade::api::command_list*>(&list), Handle(texture.Get()), Handle(texture_copy.Get()));
  context->CopyResource(texture_copy.Get(), texture.Get());
  CHECK(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
  Invoke<api_event::map_texture_region>(static_cast<reshade::api::device*>(&mock_device), Handle(staging.Get()), 0u,
                                        static_cast<const reshade::api::subresource_box*>(nullptr), reshade::api::map_access::read_only,
                                        reinterpret_cast<reshade::api::subresource_data*>(&mapped));
  Invoke<api_event::unmap_texture_region>(static_cast<reshade::api::device*>(&mock_device), Handle(staging.Get()), 0u);
  context->Unmap(staging.Get(), 0);
  // A map never unmapped in the trace, and an unmap of a map before it (only the events: the test's D3D11 calls balanced).
  Invoke<api_event::map_texture_region>(static_cast<reshade::api::device*>(&mock_device), Handle(staging.Get()), 0u,
                                        static_cast<const reshade::api::subresource_box*>(nullptr), reshade::api::map_access::read_only,
                                        reinterpret_cast<reshade::api::subresource_data*>(&mapped));
  Invoke<api_event::unmap_buffer_region>(static_cast<reshade::api::device*>(&mock_device), Handle(default_buffer.Get()));
  // ReShade's whole-buffer sizes.
  const D3D11_BUFFER_DESC staging_buffer_desc = {.ByteWidth = 1024, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
  ComPtr<ID3D11Buffer> staging_buffer;
  CHECK(SUCCEEDED(device->CreateBuffer(&staging_buffer_desc, nullptr, staging_buffer.GetAddressOf())));
  Invoke<api_event::copy_buffer_region>(static_cast<reshade::api::command_list*>(&list), Handle(default_buffer.Get()), uint64_t{0},
                                        Handle(staging_buffer.Get()), uint64_t{0}, UINT64_MAX);
  context->CopyResource(staging_buffer.Get(), default_buffer.Get());
  Invoke<api_event::update_buffer_region>(static_cast<reshade::api::device*>(&mock_device), static_cast<const void*>(bytes.data()),
                                          Handle(default_buffer.Get()), uint64_t{0}, UINT64_MAX);
  // A stack the TEB doesn't describe (a job system's, below the thread's): for one map the TEB's stack starts above
  // this frame (its DeallocationStack, GetCurrentThreadStackLimits' low limit: offset checked first). The scan must not
  // run (it would find this test's own return addresses: the control is every other map's callers).
  {
#if defined(_WIN64)
    constexpr size_t DEALLOCATION_STACK = 0x1478;
#else
    constexpr size_t DEALLOCATION_STACK = 0xE0C;
#endif
    auto* low_slot = reinterpret_cast<ULONG_PTR*>(reinterpret_cast<uint8_t*>(NtCurrentTeb()) + DEALLOCATION_STACK);
    ULONG_PTR stack_low = 0;
    ULONG_PTR stack_high = 0;
    GetCurrentThreadStackLimits(&stack_low, &stack_high);
    CHECK(*low_slot == stack_low);
    const ULONG_PTR saved_low = *low_slot;
    *low_slot = (reinterpret_cast<ULONG_PTR>(&stack_low) + 0x1000) & ~ULONG_PTR{0xFFF};
    frame_trace::internal::OnMapTraceMapBuffer(&mock_device, Handle(dynamic_buffer.Get()), 0, UINT64_MAX,
                                               reshade::api::map_access::write_discard, nullptr);
    *low_slot = saved_low;
  }
  const int64_t idle_start = Qpc();
  Sleep(5);
  const int64_t idle_end = Qpc();
  presents.push_back(Qpc());
  frame_trace::OnPresent(false);
  CHECK(Registered() == 12);
  CHECK(!frame_trace::internal::map_trace.tracing.load() && !frame_trace::internal::map_trace.armed.load());
  frame_trace::internal::OnMapTraceMapBuffer(&mock_device, Handle(dynamic_buffer.Get()), 0, UINT64_MAX, reshade::api::map_access::write_discard, nullptr);
  Invoke<api_event::draw>(static_cast<reshade::api::command_list*>(&list), 3u, 1u, 0u, 0u);
  CHECK(frame_trace::internal::map_trace.events.empty() && frame_trace::internal::map_trace.draws.empty());
  frame_trace::OnPresent(false);
  CHECK(Registered() == 12);
  frame_trace::Uninstall();
  CHECK(Registered() == 0);

  CHECK(std::filesystem::exists(trace_path));
  {
    std::ifstream file(trace_path);
    std::printf("tw_map_trace.txt:\n");
    for (std::string line; std::getline(file, line);) std::printf("  %s\n", line.c_str());
  }
  const auto header = Lines(trace_path, "trace");
  CHECK(header.size() == 1);
  CHECK(Field(header[0], "setting") == "0.15,2");
  CHECK(Field(header[0], "frames") == "2");
  CHECK(Field(header[0], "draws") == "4" && Field(header[0], "events") == "13");
  CHECK(Field(header[0], "dropped_events") == "0");
  CHECK(std::stoll(Field(header[0], "qpc_frequency")) == frequency.QuadPart);
  const auto modules = Lines(trace_path, "module");
  CHECK(modules.size() == 1 && Field(modules[0], "name") == exe_name);
  const auto present_lines = Lines(trace_path, "present");
  CHECK(present_lines.size() == 5);
  for (size_t index = 1; index < present_lines.size(); ++index) {
    CHECK(std::stoll(Field(present_lines[index], "qpc")) > std::stoll(Field(present_lines[index - 1], "qpc")));
  }
  const auto frames = Lines(trace_path, "frame");
  CHECK(frames.size() == 2);
  CHECK(Field(frames[0], "draws") == "3" && Field(frames[0], "dispatches") == "1" && Field(frames[0], "maps") == "2" && Field(frames[0], "unmaps") == "2"
        && Field(frames[0], "updates") == "1" && Field(frames[0], "copies") == "0");
  CHECK(Field(frames[1], "draws") == "0" && Field(frames[1], "maps") == "3" && Field(frames[1], "unmaps") == "2" && Field(frames[1], "copies") == "2"
        && Field(frames[1], "updates") == "1");
  CHECK(Field(present_lines[2], "qpc") == Field(frames[0], "present_qpc") && Field(present_lines[4], "qpc") == Field(header[0], "end_qpc"));
  // The test's presents bracket the trace's.
  CHECK(std::stoll(Field(frames[0], "present_qpc")) >= presents[0] && std::stoll(Field(frames[1], "present_qpc")) >= presents[1]);

  const auto maps = Lines(trace_path, "map");
  CHECK(maps.size() == 5);
  CHECK(Field(maps[0], "gap_from") == "draw" && Field(maps[1], "gap_from") == "unmap");
  for (const auto& map : maps) CHECK(Field(map, "res") != std::format("0x{:x}", reinterpret_cast<uintptr_t>(before_buffer.Get())));
  CHECK(Field(maps[0], "frame") == "0" && Field(maps[0], "access") == "discard" && Field(maps[0], "type") == "buffer" && Field(maps[0], "bytes") == "256"
        && Field(maps[0], "usage") == "dynamic" && Field(maps[0], "bind") == "0x4" && Field(maps[0], "cpu") == "0x10000");
  CHECK(Field(maps[0], "draw") == "3" && Field(maps[0], "last") == "indexed/vs:00000000/ps:00000000");
  CHECK(Field(maps[0], "unmap_ms") != "-" && std::stod(Field(maps[0], "unmap_ms")) >= std::stod(Field(maps[0], "ms")));
  CHECK(Field(maps[0], "res") == std::format("0x{:x}", reinterpret_cast<uintptr_t>(dynamic_buffer.Get())));
  CHECK(Field(maps[1], "access") == "no_overwrite" && std::stod(Field(maps[1], "gap_ms")) >= 19.);
  CHECK(std::stod(Field(maps[0], "gap_ms")) < 19.);
  CHECK(Field(maps[2], "frame") == "1" && Field(maps[2], "access") == "read" && Field(maps[2], "type") == "tex2d" && Field(maps[2], "size") == "64x32x1"
        && Field(maps[2], "format") == "28" && Field(maps[2], "usage") == "staging" && Field(maps[2], "sub") == "0" && Field(maps[2], "unmap_ms") != "-");
  CHECK(Field(maps[2], "draw") == "0" && Field(maps[2], "last") == "dispatch/cs:00000000");
  CHECK(Field(maps[3], "unmap_ms") == "-");
  CHECK(Field(maps[4], "callers") == "-" && Field(maps[4], "access") == "discard");
  for (const auto& map : std::span(maps).first(4)) {
    const std::string callers = Field(map, "callers");
    CHECK(callers.starts_with(exe_name + "+0x"));
    std::vector<std::string> each;
    for (size_t start = 0; start <= callers.size();) {
      const size_t comma = std::min(callers.find(',', start), callers.size());
      CHECK(std::ranges::find(each, callers.substr(start, comma - start)) == each.end());
      each.push_back(callers.substr(start, comma - start));
      start = comma + 1;
    }
  }
  const auto updates = Lines(trace_path, "update");
  CHECK(updates.size() == 2 && Field(updates[0], "data_bytes") == "64" && Field(updates[0], "usage") == "default" && Field(updates[0], "bytes") == "1024");
  CHECK(Field(updates[1], "data_bytes") == "0");
  const auto copies = Lines(trace_path, "copy");
  CHECK(copies.size() == 2 && Field(copies[0], "usage") == "staging" && Field(copies[0], "src_usage") == "default" && Field(copies[0], "src_type") == "tex2d"
        && Field(copies[0], "res") == std::format("0x{:x}", reinterpret_cast<uintptr_t>(staging.Get())));
  CHECK(Field(copies[1], "data_bytes") == "0" && Field(copies[1], "type") == "buffer" && Field(copies[1], "src_type") == "buffer");
  const auto unmaps = Lines(trace_path, "unmap");
  CHECK(unmaps.size() == 1 && Field(unmaps[0], "res") == std::format("0x{:x}", reinterpret_cast<uintptr_t>(default_buffer.Get()))
        && Field(unmaps[0], "usage").empty() && Field(unmaps[0], "callers") == "-");
  std::printf("2. two frames traced (comma-decimal locale %s): %zu maps, %zu updates, %zu copies, %zu unmatched unmaps; registered at Install,"
              " gated off outside them\n",
              (comma_locale ? "set" : "not available"), maps.size(), updates.size(), copies.size(), unmaps.size());

  // 3. The join's input: another clock (microseconds, 1234.5678 s behind).
  const auto us = [&](int64_t qpc) { return static_cast<double>(qpc) * 1e6 / static_cast<double>(frequency.QuadPart) - 1234567800.; };
  std::ofstream waits(folder / L"waits.txt", std::ios::trunc);
  for (const auto& present : present_lines) waits << std::format("present {:.1f}\n", us(std::stoll(Field(present, "qpc"))) + 50.);
  waits << std::format("wait {:.1f} {:.1f} {} 0x179b980(+0x179b9b3)\n", us(wait_start) + 100., us(wait_end) - 100., GetCurrentThreadId());
  waits << std::format("wait {:.1f} {:.1f} {} idle\n", us(idle_start) + 100., us(idle_end) - 100., GetCurrentThreadId());
  std::printf("3. waits.txt written\nPASS\n");
  return 0;
}
