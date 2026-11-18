// Time Each Pass (src/games/twattila/pass_timing.hpp) on WARP, through ReShade API mocks (tw_sampler_lifetime's): its
// phases from the button to the file, with draws that only reach the add-on's event (no GPU work between the stamps).
// 1. A first run: one counting frame, the stamps made a chunk a present (commands x 1.25 + 1024, + 1), the timed frame,
//    the file with every command.
// 2. A second run: the stamps kept, the timed frame right after the counting frame.
// 3. A timed frame with more commands than stamps: more made and another frame timed, one file; meanwhile a present on
//    another device changes nothing.
// 4. A frame over MAX_COMMANDS: the first MAX_COMMANDS saved, said in the status; again the same (no endless retry).
// 5. CreateQuery failing part way (its vtable slot patched in this process): the frame timed with the stamps made, its
//    first commands saved; a second run the same.
// 6. Another device destroyed mid-run: nothing changes; the run's device destroyed: the run stops, stamps released.
#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>

#include "src/games/twattila/pass_timing.hpp"
extern "C" __declspec(dllexport) const char* const NAME = "Pass timing test";
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}
#include "test/tw_sampler_lifetime/mock_reshade.hpp"

namespace timing = twattila::pass_timing;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::fflush(stdout); ExitProcess(1); } } while (false)

// The command list without tracked state: every shader hash 0.
struct Commands : mock::CommandList {
  void get_private_data(const uint8_t[16], uint64_t* data) const override { *data = 0; }
};

struct Rig {
  mock::Device device;
  Commands commands;
  mock::Queue queue;
};

static void Make(Rig* rig) {
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &rig->device.native,
                                    nullptr, &rig->commands.native)));
  rig->commands.owner = &rig->device;
  rig->queue.commands = &rig->commands;
}

// One frame: `draws` draw events, then the present.
static void Frame(Rig* rig, uint32_t draws) {
  for (uint32_t draw = 0; draw < draws; ++draw) timing::internal::OnDraw(&rig->commands, 3, 1, 0, 0);
  timing::OnPresent(&rig->queue, nullptr, nullptr, nullptr, 0, nullptr);
  rig->commands.native->Flush();
}

static timing::internal::Phase Phase() {
  const std::lock_guard lock(timing::internal::state.mutex);
  return timing::internal::state.phase;
}

static size_t Stamps() {
  const std::lock_guard lock(timing::internal::state.mutex);
  return timing::internal::state.stamps.size();
}

// A run from the button: `counted` draws in the counting frame, `timed` in every other. Returns the presents it took.
static int Run(Rig* rig, uint32_t counted, uint32_t timed, const std::function<void(int)>& each_present = nullptr) {
  timing::Request();
  int presents = 0;
  while (Phase() != timing::internal::Phase::IDLE) {
    CHECK(presents < 2000);
    if (Phase() == timing::internal::Phase::READING) Sleep(1);
    Frame(rig, (Phase() == timing::internal::Phase::COUNTING ? counted : timed));
    ++presents;
    if (each_present) each_present(presents);
  }
  return presents;
}

// The saved file's command lines and its header's count.
static std::pair<size_t, size_t> FileCommands() {
  std::wstring exe(32768, L'\0');
  exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
  const auto path = std::filesystem::path(exe).parent_path() / "ia5_pass_timing_test_pass_timing.txt";
  std::ifstream file(path, std::ios::binary);
  std::stringstream text;
  text << file.rdbuf();
  file.close();
  const std::string content = text.str();
  constexpr std::string_view HEADER = " ms from the first draw or dispatch to the present, ";
  size_t header = 0;
  if (const size_t at = content.find(HEADER); at != std::string::npos) header = std::stoul(content.substr(at + HEADER.size()));
  size_t lines = 0;
  if (size_t at = content.find("\n== Commands ==\n"); at != std::string::npos) {
    at = content.find('\n', at + 16);  // the column names
    for (; at != std::string::npos && at + 1 < content.size(); at = content.find('\n', at + 1)) ++lines;
  }
  std::filesystem::remove(path);
  return {lines, header};
}

// ID3D11Device::CreateQuery (vtable slot 24) letting `queries_left` timestamps through, then failing.
using CreateQueryFunction = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const D3D11_QUERY_DESC*, ID3D11Query**);
static CreateQueryFunction real_create_query = nullptr;
static std::atomic<int> queries_left = 0;
static HRESULT STDMETHODCALLTYPE LimitedCreateQuery(ID3D11Device* device, const D3D11_QUERY_DESC* desc, ID3D11Query** query) {
  if (desc->Query == D3D11_QUERY_TIMESTAMP && queries_left.fetch_sub(1) <= 0) return E_OUTOFMEMORY;
  return real_create_query(device, desc, query);
}
static void SetCreateQuery(ID3D11Device* device, void* function) {
  void** vtable = *reinterpret_cast<void***>(device);
  DWORD protection = 0;
  CHECK(VirtualProtect(&vtable[24], sizeof(void*), PAGE_READWRITE, &protection));
  vtable[24] = function;
  CHECK(VirtualProtect(&vtable[24], sizeof(void*), protection, &protection));
}

static int Body();

// The mocks throw on a call they don't expect.
int main() {
  try {
    return Body();
  } catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
  }
}

static int Body() {
  timing::internal::state.prefix = "ia5_pass_timing_test";
  Rig first;
  Rig second;
  Rig third;
  Make(&first);
  Make(&second);
  Make(&third);

  // 1.
  {
    const int presents = Run(&first, 2500, 2500);
    std::printf("1 first run, 2500 commands: %d presents, %zu stamps, status \"%s\"\n", presents, Stamps(), timing::Status().c_str());
    CHECK(Stamps() == 2500 + 625 + 1024 + 1);
    // The request, the count + 1000, 3 x 1000, the last 150 + the timed frame's start, its end, then the read (WARP may
    // take a few presents).
    CHECK(presents >= 8 && presents < 8 + 100);
    CHECK(timing::Status().starts_with("Saved: ") && timing::Status().find(", 2500 commands, ") != std::string::npos);
    const auto [lines, header] = FileCommands();
    CHECK(lines == 2500 && header == 2500);
  }
  // 2.
  {
    const int presents = Run(&first, 2400, 2400);
    std::printf("2 second run, 2400 commands: %d presents, %zu stamps, status \"%s\"\n", presents, Stamps(), timing::Status().c_str());
    CHECK(Stamps() == 4150 && presents >= 4 && presents < 4 + 100);  // the request, the count + the start, the end, the read
    const auto [lines, header] = FileCommands();
    CHECK(lines == 2400 && header == 2400);
  }
  // 3. Counted 100, timed 6000 (4149 stamped): stamps for 6000, another frame timed. The other device presents at each
  //    of the first run's presents while it prepares.
  {
    int other_presents = 0;
    const int presents = Run(&first, 100, 6000, [&](int) {
      if (Phase() != timing::internal::Phase::PREPARING) return;
      const size_t stamps = Stamps();
      Frame(&second, 50);
      ++other_presents;
      CHECK(Stamps() == stamps && Phase() == timing::internal::Phase::PREPARING);
    });
    std::printf("3 counted 100, timed 6000: %d presents, %zu stamps, %d presents of another device meanwhile, status \"%s\"\n",
                presents, Stamps(), other_presents, timing::Status().c_str());
    CHECK(Stamps() == 6000 + 1500 + 1024 + 1 && other_presents > 0);
    CHECK(timing::Status().find(", 6000 commands, ") != std::string::npos);
    const auto [lines, header] = FileCommands();
    CHECK(lines == 6000 && header == 6000);
  }
  // 4. 45000 commands, more than MAX_COMMANDS: the first 40000 saved, twice.
  for (int round = 0; round < 2; ++round) {
    const int presents = Run(&first, 45000, 45000);
    std::printf("4 over the cap (%d), 45000 commands: %d presents, %zu stamps, status \"%s\"\n", round, presents, Stamps(),
                timing::Status().c_str());
    CHECK(Stamps() == timing::internal::MAX_COMMANDS + 1);
    CHECK(timing::Status().find(", first 40000 of 45000 commands, ") != std::string::npos);
    const auto [lines, header] = FileCommands();
    CHECK(lines == 40000 && header == 40000);
  }
  // 5. The third device (its first run: the first device's stamps released): 2500 timestamps made, then none.
  {
    real_create_query = reinterpret_cast<CreateQueryFunction>((*reinterpret_cast<void***>(third.device.native.Get()))[24]);
    SetCreateQuery(third.device.native.Get(), reinterpret_cast<void*>(&LimitedCreateQuery));
    queries_left = 2500;
    for (int round = 0; round < 2; ++round) {
      const int presents = Run(&third, 3000, 3000);
      std::printf("5 CreateQuery failing after 2500 (%d), 3000 commands: %d presents, %zu stamps, status \"%s\"\n", round, presents,
                  Stamps(), timing::Status().c_str());
      CHECK(Stamps() == 2500);
      CHECK(timing::Status().find(", first 2499 of 3000 commands, ") != std::string::npos);
      const auto [lines, header] = FileCommands();
      CHECK(lines == 2499 && header == 2499);
    }
    SetCreateQuery(third.device.native.Get(), reinterpret_cast<void*>(real_create_query));
  }
  // 6. A run on the second device stopped by destroys while it prepares.
  {
    timing::Request();
    Frame(&second, 0);
    Frame(&second, 5000);
    CHECK(Phase() == timing::internal::Phase::PREPARING && Stamps() == 1000);
    timing::OnDestroyDevice(&third.device);
    CHECK(Phase() == timing::internal::Phase::PREPARING && Stamps() == 1000);
    timing::OnDestroyDevice(&second.device);
    std::printf("6 destroys mid-run: another device's left it preparing with 1000 stamps; its own: %zu stamps, status \"%s\"\n",
                Stamps(), timing::Status().c_str());
    CHECK(Phase() == timing::internal::Phase::IDLE && Stamps() == 0);
    CHECK(timing::Status() == "The game's device went away during the run: run it again.");
    const int presents = Run(&second, 1000, 1000);
    CHECK(timing::Status().find(", 1000 commands, ") != std::string::npos && presents >= 6 && presents < 6 + 100);
    const auto [lines, header] = FileCommands();
    CHECK(lines == 1000 && header == 1000);
    std::printf("6 a run after it: %d presents, status \"%s\"\n", presents, timing::Status().c_str());
  }
  timing::OnDestroyDevice(nullptr);
  std::printf("PASS\n");
  return 0;
}
