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
//    unmapped, an unmap of a buffer mapped before the trace, a map whose frame lies below its stack limits (the trace's
//    stack-limits seam moves the low limit to the stack's top: a job system's own stack: no scan, callers=-). Every
//    other map's first callers in the exe's code, each once.
//    No present event before it: no immediate context, native_hook=no_context, no native call.
// 3. waits.txt for tools\tw\map_trace.py's join (run.ps1): the presents on another clock (QPC microseconds minus an
//    offset) and two waits on this thread, one inside the NO_OVERWRITE map's gap, one between events.
// 4. Map trace v2, the native hook (RENODX_TW_MAP_TRACE=0,1): the present event gives the WARP immediate context; the
//    test's own "slow driver" hook already sits in its Map slot (15 ms inside the call for one buffer). The first present
//    starts the frame and puts the trace's hook into the Map and Unmap slots (above the test's); every Map and Unmap on
//    that context is a native_map / native_unmap line: a constant buffer's DISCARD (native_ms >= 14: the slow driver's
//    wait, inside the call), a vertex buffer's NO_OVERWRITE after a 15 ms sleep before the call (CONTROL: native_ms
//    under 5, the time before the call is not the call's), an index buffer's DISCARD, a dynamic texture's DISCARD (its
//    depth pitch), a structured buffer's DISCARD (other), a staging buffer's READ with DO_NOT_WAIT (its flags and
//    result), an invalid DISCARD of a staging buffer (failed, its HRESULT), an unmap of a map made before the trace (a
//    native_unmap line); each map's class, type, flags, bytes and unmap; the direct caller and the game's callers in
//    the exe. CONTROLS: a second WARP device's immediate context (its vtable the same) and a deferred context map too,
//    none recorded. The native_frame line's totals (counts, failed, DISCARD bytes, per class). After the frame: both
//    slots back to what was there (the test's slow Map, the runtime's Unmap); Uninstall unregisters all 13.
// 5. Another hook takes the Map slot over above the trace's: at the trace's end the slot can't go back (logged: "stays
//    chained"), the Unmap slot goes back; Maps through the chain still work and record nothing.
// 6. A long trace past the native calls' room: the drops counted, the file (tens of MB) written in parts, every kept
//    map in it in order, the totals of the kept ones.
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
static std::vector<std::string> log_lines;
__declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) {
  std::printf("  log: %s\n", message);
  log_lines.emplace_back(message);
}
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

// The present event's queue: the immediate context of a mocked device.
struct TestQueue : reshade::api::command_queue {
  mock::Device* owner = nullptr;
  mock::CommandList* list = nullptr;
  uint64_t get_native() const override { return list->get_native(); }
  void get_private_data(const uint8_t[16], uint64_t* data) const override { *data = 0; }
  void set_private_data(const uint8_t[16], const uint64_t) override {}
  reshade::api::device* get_device() override { return owner; }
  reshade::api::command_queue_type get_type() const override { return reshade::api::command_queue_type::graphics; }
  void wait_idle() const override {}
  void flush_immediate_command_list() const override {}
  reshade::api::command_list* get_immediate_command_list() override { return list; }
  void begin_debug_event(const char*, const float[4]) override {}
  void end_debug_event() override {}
  void insert_debug_marker(const char*, const float[4]) override {}
  bool wait(reshade::api::fence, uint64_t) override { return false; }
  bool signal(reshade::api::fence, uint64_t) override { return false; }
  uint64_t get_timestamp_frequency() const override { return 0; }
};

// The test's own hooks in the Map slot: a driver that waits 15 ms inside Map for one resource (below the trace's hook),
// and another add-on's hook that takes the slot over above it (part 5).
using MapFunction = frame_trace::internal::NativeMapFunction;
static ID3D11Resource* slow_resource = nullptr;
static MapFunction slow_next = nullptr;
static HRESULT STDMETHODCALLTYPE SlowMap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, D3D11_MAP type, UINT flags,
                                         D3D11_MAPPED_SUBRESOURCE* mapped) {
  if (resource == slow_resource) Sleep(15);
  return slow_next(context, resource, subresource, type, flags, mapped);
}
static MapFunction intruder_next = nullptr;
static int intruder_calls = 0;
static HRESULT STDMETHODCALLTYPE IntruderMap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, D3D11_MAP type, UINT flags,
                                             D3D11_MAPPED_SUBRESOURCE* mapped) {
  ++intruder_calls;
  return intruder_next(context, resource, subresource, type, flags, mapped);
}

static void* TableEntry(ID3D11DeviceContext* context, size_t index) { return (*reinterpret_cast<void***>(context))[index]; }

// No tracked state: every shader hash 0.
struct TestList : mock::CommandList {
  void get_private_data(const uint8_t[16], uint64_t* data) const override { *data = 0; }
  void set_private_data(const uint8_t[16], const uint64_t) override {}
};

static size_t Registered() {
  size_t count = 0;
  for (const auto ev : {api_event::draw, api_event::draw_indexed, api_event::dispatch, api_event::map_buffer_region, api_event::map_texture_region,
                        api_event::unmap_buffer_region, api_event::unmap_texture_region, api_event::update_buffer_region, api_event::copy_resource,
                        api_event::copy_buffer_region, api_event::copy_texture_region, api_event::draw_or_dispatch_indirect, api_event::present}) {
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

// The map trace's stack-limits seam for one map: this thread's stack with its low limit at the top, so every frame
// below lies outside it.
static void WINAPI StackAboveThisFrame(PULONG_PTR low, PULONG_PTR high) {
  GetCurrentThreadStackLimits(low, high);
  *low = *high - sizeof(ULONG_PTR);
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
  CHECK(Registered() == 13);
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
  CHECK(Registered() == 13);
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
  CHECK(Registered() == 13);

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
  // A stack its limits don't describe (a job system's, below the thread's): for one map the trace's stack-limits seam
  // returns this thread's stack with its low limit moved to the top (the callback's frame below it). The scan must not
  // run (it would find this test's own return addresses: the control is every other map's callers).
  frame_trace::internal::map_trace.stack_limits = StackAboveThisFrame;
  frame_trace::internal::OnMapTraceMapBuffer(&mock_device, Handle(dynamic_buffer.Get()), 0, UINT64_MAX,
                                             reshade::api::map_access::write_discard, nullptr);
  frame_trace::internal::map_trace.stack_limits = GetCurrentThreadStackLimits;
  const int64_t idle_start = Qpc();
  Sleep(5);
  const int64_t idle_end = Qpc();
  presents.push_back(Qpc());
  frame_trace::OnPresent(false);
  CHECK(Registered() == 13);
  CHECK(!frame_trace::internal::map_trace.tracing.load() && !frame_trace::internal::map_trace.armed.load());
  frame_trace::internal::OnMapTraceMapBuffer(&mock_device, Handle(dynamic_buffer.Get()), 0, UINT64_MAX, reshade::api::map_access::write_discard, nullptr);
  Invoke<api_event::draw>(static_cast<reshade::api::command_list*>(&list), 3u, 1u, 0u, 0u);
  CHECK(frame_trace::internal::map_trace.events.empty() && frame_trace::internal::map_trace.draws.empty());
  frame_trace::OnPresent(false);
  CHECK(Registered() == 13);
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
  CHECK(Field(header[0], "native_hook") == "no_context" && Field(header[0], "native_calls") == "0");
  CHECK(Lines(trace_path, "native_map").empty() && Lines(trace_path, "native_frame").size() == 2);
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
  waits.close();
  std::printf("3. waits.txt written\n");

  // 4. The native hook. Part 2's trace kept for run.ps1 (map_trace.py's summary and join), this part's beside it.
  const auto part2_path = folder / L"tw_map_trace_part2.txt";
  const auto native_path = folder / L"tw_map_trace_native.txt";
  std::filesystem::rename(trace_path, part2_path);
  std::filesystem::remove(native_path);
  const auto describe_buffer = [&](UINT bytes, D3D11_USAGE usage, UINT bind, UINT cpu, UINT misc = 0, UINT stride = 0) {
    const D3D11_BUFFER_DESC desc = {.ByteWidth = bytes, .Usage = usage, .BindFlags = bind, .CPUAccessFlags = cpu, .MiscFlags = misc,
                                    .StructureByteStride = stride};
    ComPtr<ID3D11Buffer> buffer;
    if (FAILED(device->CreateBuffer(&desc, nullptr, buffer.GetAddressOf()))) std::printf("  CreateBuffer failed (%u bytes)\n", bytes);
    return buffer;
  };
  const auto constant_buffer = describe_buffer(256, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE);
  const auto vertex_buffer = describe_buffer(4096, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE);
  const auto index_buffer = describe_buffer(1024, D3D11_USAGE_DYNAMIC, D3D11_BIND_INDEX_BUFFER, D3D11_CPU_ACCESS_WRITE);
  const auto structured_buffer = describe_buffer(2048, D3D11_USAGE_DYNAMIC, D3D11_BIND_SHADER_RESOURCE, D3D11_CPU_ACCESS_WRITE,
                                                 D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 16);
  const auto readback_buffer = describe_buffer(1024, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ);
  const auto early_buffer = describe_buffer(512, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE);
  CHECK(constant_buffer && vertex_buffer && index_buffer && structured_buffer && readback_buffer && early_buffer);
  const D3D11_TEXTURE2D_DESC dynamic_texture_desc = {.Width = 64, .Height = 32, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                                                     .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DYNAMIC, .BindFlags = D3D11_BIND_SHADER_RESOURCE,
                                                     .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE};
  ComPtr<ID3D11Texture2D> dynamic_texture;
  CHECK(SUCCEEDED(device->CreateTexture2D(&dynamic_texture_desc, nullptr, dynamic_texture.GetAddressOf())));
  // The other contexts: a second device's immediate one, a deferred one of this device.
  ComPtr<ID3D11Device> other_device;
  ComPtr<ID3D11DeviceContext> other_context;
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, other_device.GetAddressOf(), nullptr,
                                    other_context.GetAddressOf())));
  const D3D11_BUFFER_DESC other_desc = {.ByteWidth = 256, .Usage = D3D11_USAGE_DYNAMIC, .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
                                        .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE};
  ComPtr<ID3D11Buffer> other_buffer;
  CHECK(SUCCEEDED(other_device->CreateBuffer(&other_desc, nullptr, other_buffer.GetAddressOf())));
  ComPtr<ID3D11DeviceContext> deferred_context;
  CHECK(SUCCEEDED(device->CreateDeferredContext(0, deferred_context.GetAddressOf())));
  const bool shared_table = TableEntry(other_context.Get(), 14) == TableEntry(context.Get(), 14);

  void* const runtime_map = TableEntry(context.Get(), 14);
  void* const runtime_unmap = TableEntry(context.Get(), 15);
  renodx::utils::vtable::Slot<MapFunction> slow_slot = {.object = context.Get(), .index = 14, .original = &slow_next, .replacement = &SlowMap};
  renodx::utils::vtable::Install(&slow_slot);
  slow_resource = constant_buffer.Get();
  TestQueue queue;
  queue.owner = &mock_device;
  queue.list = &list;
  SetEnvironmentVariableW(L"RENODX_TW_MAP_TRACE", L"0,1");
  frame_trace::Install();
  CHECK(Registered() == 13);
  CHECK(SUCCEEDED(context->Map(early_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
  CHECK(frame_trace::internal::map_trace.immediate_context.load() == nullptr);
  Invoke<api_event::present>(static_cast<reshade::api::command_queue*>(&queue), static_cast<reshade::api::swapchain*>(nullptr),
                             static_cast<const reshade::api::rect*>(nullptr), static_cast<const reshade::api::rect*>(nullptr), 0u,
                             static_cast<const reshade::api::rect*>(nullptr));
  CHECK(frame_trace::internal::map_trace.immediate_context.load() == context.Get());
  frame_trace::OnPresent(false);
  CHECK(frame_trace::internal::map_trace.tracing.load());
  CHECK(TableEntry(context.Get(), 14) == reinterpret_cast<void*>(&frame_trace::internal::NativeMap));
  CHECK(TableEntry(context.Get(), 15) == reinterpret_cast<void*>(&frame_trace::internal::NativeUnmap));

  struct Expected {
    std::string res;
    std::string map;
    std::string map_class;
    HRESULT result = S_OK;
    uint32_t bytes = 0;
    uint32_t flags = 0;
  };
  std::vector<Expected> expected;
  const auto res_text = [](ID3D11Resource* resource) { return std::format("0x{:x}", reinterpret_cast<uintptr_t>(resource)); };
  context->Unmap(early_buffer.Get(), 0);
  CHECK(SUCCEEDED(context->Map(constant_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
  context->Unmap(constant_buffer.Get(), 0);
  expected.push_back({.res = res_text(constant_buffer.Get()), .map = "discard", .map_class = "constant", .bytes = 256});
  Sleep(15);
  CHECK(SUCCEEDED(context->Map(vertex_buffer.Get(), 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped)));
  context->Unmap(vertex_buffer.Get(), 0);
  expected.push_back({.res = res_text(vertex_buffer.Get()), .map = "no_overwrite", .map_class = "vertex", .bytes = 4096});
  CHECK(SUCCEEDED(context->Map(index_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
  context->Unmap(index_buffer.Get(), 0);
  expected.push_back({.res = res_text(index_buffer.Get()), .map = "discard", .map_class = "index", .bytes = 1024});
  CHECK(SUCCEEDED(context->Map(dynamic_texture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
  const uint32_t texture_bytes = mapped.DepthPitch;
  context->Unmap(dynamic_texture.Get(), 0);
  CHECK(texture_bytes >= 64 * 4 * 32);
  expected.push_back({.res = res_text(dynamic_texture.Get()), .map = "discard", .map_class = "texture", .bytes = texture_bytes});
  CHECK(SUCCEEDED(context->Map(structured_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
  context->Unmap(structured_buffer.Get(), 0);
  expected.push_back({.res = res_text(structured_buffer.Get()), .map = "discard", .map_class = "other", .bytes = 2048});
  context->CopyResource(readback_buffer.Get(), vertex_buffer.Get());
  const HRESULT poll = context->Map(readback_buffer.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
  if (SUCCEEDED(poll)) context->Unmap(readback_buffer.Get(), 0);
  expected.push_back({.res = res_text(readback_buffer.Get()), .map = "read", .map_class = "other", .result = poll,
                      .bytes = (SUCCEEDED(poll) ? 1024u : 0u), .flags = D3D11_MAP_FLAG_DO_NOT_WAIT});
  const HRESULT invalid = context->Map(readback_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  CHECK(FAILED(invalid));
  expected.push_back({.res = res_text(readback_buffer.Get()), .map = "discard", .map_class = "other", .result = invalid});
  // CONTROLS: other contexts' maps through the same slot (when their table is the same) are not the game's.
  CHECK(SUCCEEDED(other_context->Map(other_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
  other_context->Unmap(other_buffer.Get(), 0);
  CHECK(SUCCEEDED(deferred_context->Map(vertex_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
  deferred_context->Unmap(vertex_buffer.Get(), 0);
  frame_trace::OnPresent(false);
  CHECK(!frame_trace::internal::map_trace.tracing.load());
  CHECK(TableEntry(context.Get(), 14) == reinterpret_cast<void*>(&SlowMap));
  CHECK(TableEntry(context.Get(), 15) == runtime_unmap);
  CHECK(frame_trace::internal::map_trace.native_calls.empty());
  frame_trace::Uninstall();
  CHECK(Registered() == 0);
  renodx::utils::vtable::Uninstall(&slow_slot);
  CHECK(TableEntry(context.Get(), 14) == runtime_map);
  std::filesystem::rename(trace_path, native_path);
  {
    std::ifstream file(native_path);
    std::printf("tw_map_trace_native.txt (native lines):\n");
    for (std::string line; std::getline(file, line);) {
      if (line.starts_with("native") || line.starts_with("trace")) std::printf("  %s\n", line.c_str());
    }
  }
  const auto native_header = Lines(native_path, "trace");
  CHECK(native_header.size() == 1 && Field(native_header[0], "native_hook") == "on");
  const auto native_maps = Lines(native_path, "native_map");
  CHECK(Field(native_header[0], "native_calls") == std::to_string(expected.size() + 6 + (SUCCEEDED(poll) ? 1 : 0))
        && Field(native_header[0], "dropped_native_calls") == "0");
  CHECK(native_maps.size() == expected.size());
  for (size_t index = 0; index < expected.size(); ++index) {
    const auto& line = native_maps[index];
    const auto& want = expected[index];
    CHECK(Field(line, "frame") == "0" && Field(line, "res") == want.res && Field(line, "map") == want.map && Field(line, "class") == want.map_class);
    CHECK(Field(line, "hr") == std::format("0x{:08x}", static_cast<uint32_t>(want.result)));
    CHECK(Field(line, "map_flags") == std::format("0x{:x}", want.flags) && Field(line, "map_bytes") == std::to_string(want.bytes));
    CHECK((Field(line, "unmap_ms") != "-") == (SUCCEEDED(want.result)) && (Field(line, "unmap_native_ms") != "-") == SUCCEEDED(want.result));
    CHECK(Field(line, "direct").starts_with(exe_name + "+0x") && Field(line, "callers").starts_with(exe_name + "+0x"));
    CHECK(Field(line, "thread") == std::to_string(GetCurrentThreadId()));
  }
  CHECK(Field(native_maps[0], "usage") == "dynamic" && Field(native_maps[0], "bind") == "0x4" && Field(native_maps[0], "type") == "buffer"
        && Field(native_maps[0], "bytes") == "256");
  CHECK(Field(native_maps[3], "type") == "tex2d" && Field(native_maps[3], "size") == "64x32x1");
  CHECK(std::stod(Field(native_maps[0], "native_ms")) >= 14.);
  // CONTROL: the 15 ms sleep just before the vertex buffer's Map is outside its native call.
  CHECK(std::stod(Field(native_maps[1], "native_ms")) < 5.);
  for (const auto& line : native_maps) CHECK(Field(line, "res") != res_text(other_buffer.Get()));
  CHECK(std::ranges::count_if(native_maps, [&](const std::string& line) { return Field(line, "res") == res_text(vertex_buffer.Get()); }) == 1);
  const auto native_unmaps = Lines(native_path, "native_unmap");
  CHECK(native_unmaps.size() == 1 && Field(native_unmaps[0], "res") == res_text(early_buffer.Get()) && Field(native_unmaps[0], "direct").starts_with(exe_name + "+0x"));
  const auto native_frames = Lines(native_path, "native_frame");
  CHECK(native_frames.size() == 1);
  const auto& totals = native_frames[0];
  const int failed = 1 + (FAILED(poll) ? 1 : 0);
  CHECK(Field(totals, "maps") == std::to_string(expected.size()) && Field(totals, "failed") == std::to_string(failed));
  CHECK(Field(totals, "unmaps") == std::to_string(6 + (SUCCEEDED(poll) ? 1 : 0)));
  CHECK(Field(totals, "discards") == "4" && Field(totals, "no_overwrites") == "1" && Field(totals, "reads") == std::to_string(SUCCEEDED(poll) ? 1 : 0));
  CHECK(Field(totals, "discard_bytes") == std::to_string(256 + 1024 + texture_bytes + 2048));
  CHECK(Field(totals, "constant_maps") == "1" && Field(totals, "constant_discards") == "1" && Field(totals, "constant_discard_bytes") == "256"
        && std::stod(Field(totals, "constant_ms")) >= 14.);
  CHECK(Field(totals, "vertex_maps") == "1" && Field(totals, "vertex_discards") == "0" && Field(totals, "index_maps") == "1"
        && Field(totals, "index_discard_bytes") == "1024");
  CHECK(Field(totals, "texture_maps") == "1" && Field(totals, "texture_discard_bytes") == std::to_string(texture_bytes));
  CHECK(Field(totals, "other_maps") == "3" && Field(totals, "other_discards") == "1" && Field(totals, "other_discard_bytes") == "2048");
  CHECK(std::stod(Field(totals, "native_ms")) >= std::stod(Field(totals, "constant_ms")));
  std::printf("4. native hook: %zu maps (%d failed, the poll 0x%08x), 1 unmap without its map, totals per class; the slow driver's 15 ms inside "
              "the call, the sleep before one outside it; other contexts (second device's table %s) and the deferred one not recorded; both "
              "slots back\n",
              native_maps.size(), failed, static_cast<uint32_t>(poll), (shared_table ? "shared" : "its own"));

  // 5. Another hook over the trace's Map slot.
  SetEnvironmentVariableW(L"RENODX_TW_MAP_TRACE", L"0,1");
  frame_trace::Install();
  Invoke<api_event::present>(static_cast<reshade::api::command_queue*>(&queue), static_cast<reshade::api::swapchain*>(nullptr),
                             static_cast<const reshade::api::rect*>(nullptr), static_cast<const reshade::api::rect*>(nullptr), 0u,
                             static_cast<const reshade::api::rect*>(nullptr));
  frame_trace::OnPresent(false);
  CHECK(TableEntry(context.Get(), 14) == reinterpret_cast<void*>(&frame_trace::internal::NativeMap));
  renodx::utils::vtable::Slot<MapFunction> intruder_slot = {.object = context.Get(), .index = 14, .original = &intruder_next, .replacement = &IntruderMap};
  renodx::utils::vtable::Install(&intruder_slot);
  log_lines.clear();
  frame_trace::OnPresent(false);
  CHECK(std::ranges::any_of(log_lines, [](const std::string& line) { return line.find("stays chained") != std::string::npos; }));
  CHECK(TableEntry(context.Get(), 14) == reinterpret_cast<void*>(&IntruderMap) && TableEntry(context.Get(), 15) == runtime_unmap);
  CHECK(SUCCEEDED(context->Map(constant_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
  context->Unmap(constant_buffer.Get(), 0);
  CHECK(intruder_calls == 1 && frame_trace::internal::map_trace.native_calls.empty());
  frame_trace::Uninstall();
  CHECK(Registered() == 0);
  // The intruder leaves (the trace's hook back in its place, passing on), then the runtime's Map by hand.
  renodx::utils::vtable::Uninstall(&intruder_slot);
  CHECK(TableEntry(context.Get(), 14) == reinterpret_cast<void*>(&frame_trace::internal::NativeMap));
  CHECK(SUCCEEDED(context->Map(constant_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
  context->Unmap(constant_buffer.Get(), 0);
  {
    void** entry = *reinterpret_cast<void***>(context.Get()) + 14;
    DWORD protection = 0;
    CHECK(VirtualProtect(entry, sizeof(void*), PAGE_READWRITE, &protection) != FALSE);
    *entry = runtime_map;
    VirtualProtect(entry, sizeof(void*), protection, &protection);
  }
  std::printf("5. a hook over the trace's: the slot stays chained (logged), the unmap slot back, maps still work and record nothing\n");

  // 6. A long trace: 70,000 maps and unmaps on one frame, more than the native calls' room (2^17): the rest counted as
  // dropped, the file written in parts (tens of MB), every kept map's line in it, in order.
  SetEnvironmentVariableW(L"RENODX_TW_MAP_TRACE", L"0,1");
  frame_trace::Install();
  Invoke<api_event::present>(static_cast<reshade::api::command_queue*>(&queue), static_cast<reshade::api::swapchain*>(nullptr),
                             static_cast<const reshade::api::rect*>(nullptr), static_cast<const reshade::api::rect*>(nullptr), 0u,
                             static_cast<const reshade::api::rect*>(nullptr));
  frame_trace::OnPresent(false);
  constexpr size_t LONG_MAPS = 70000;
  for (size_t index = 0; index < LONG_MAPS; ++index) {
    CHECK(SUCCEEDED(context->Map(vertex_buffer.Get(), 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped)));
    context->Unmap(vertex_buffer.Get(), 0);
  }
  frame_trace::OnPresent(false);
  frame_trace::Uninstall();
  CHECK(TableEntry(context.Get(), 14) == runtime_map && TableEntry(context.Get(), 15) == runtime_unmap);
  const size_t kept_calls = frame_trace::internal::MAX_NATIVE_CALLS;
  const auto long_header = Lines(trace_path, "trace");
  CHECK(long_header.size() == 1 && Field(long_header[0], "native_calls") == std::to_string(kept_calls)
        && Field(long_header[0], "dropped_native_calls") == std::to_string(2 * LONG_MAPS - kept_calls));
  const auto long_maps = Lines(trace_path, "native_map");
  CHECK(long_maps.size() == kept_calls / 2);
  CHECK(std::ranges::is_sorted(long_maps, {}, [](const std::string& line) { return std::stoll(Field(line, "qpc")); }));
  const auto long_frames = Lines(trace_path, "native_frame");
  CHECK(long_frames.size() == 1 && Field(long_frames[0], "maps") == std::to_string(kept_calls / 2)
        && Field(long_frames[0], "vertex_maps") == std::to_string(kept_calls / 2) && Field(long_frames[0], "no_overwrites") == std::to_string(kept_calls / 2));
  const auto long_bytes = std::filesystem::file_size(trace_path);
  CHECK(long_bytes > (size_t{1} << 23));
  std::printf("6. a long trace: %zu native maps kept, %zu calls dropped, %llu bytes written in parts\n", long_maps.size(), 2 * LONG_MAPS - kept_calls,
              static_cast<unsigned long long>(long_bytes));

  // Part 2's trace back in its place for run.ps1.
  std::filesystem::remove(trace_path);
  std::filesystem::rename(part2_path, trace_path);
  std::printf("PASS\n");
  return 0;
}
