#include <windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x)                                                       \
  do {                                                                  \
    if (!(x)) {                                                         \
      std::printf("FAIL %d: %s (%lu)\n", __LINE__, #x, GetLastError()); \
      std::exit(1);                                                     \
    }                                                                   \
  } while (false)
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) { std::puts(message); }
static volatile int calls = 0;
__declspec(noinline) static float* __fastcall Distances(void*, void*, float* out, uint32_t, uint32_t, uint32_t, uint32_t) {
  calls = calls + 1;
  for (int i = 0; i < 18; ++i) out[i] = 10.f;
  return out;
}
__declspec(noinline) static void __fastcall Scrub(uint8_t*, void*, uint32_t, const float*, uint32_t, uint32_t, uint32_t) {
  calls = calls + 1;
}
__declspec(noinline) static void* __fastcall Lod(void*, void*, float distance, void**) {
  calls = calls + 1;
  return reinterpret_cast<void*>(static_cast<uintptr_t>(distance));
}
// As the game's engine values: in the game's image, which stays while the addon's goes.
static float scaled_value = 10.f;
static float reset_value = 10.f;
static uint8_t flag_value = 0;
static float constant_value = 10.f;
int main(int argc, char** argv) {
  CHECK(argc == 2);
  const int stage = std::atoi(argv[1]);
  auto distance_call = &Distances;
  decltype(distance_call) volatile invoke_distance = distance_call;
  decltype(&Scrub) volatile invoke_scrub = &Scrub;
  decltype(&Lod) volatile invoke_lod = &Lod;
  std::array<float, 18> out;
  HMODULE module = LoadLibraryW(L"native_hooks_test.dll");
  CHECK(module);
  auto install = reinterpret_cast<int (*)(void*, void*)>(GetProcAddress(module, "InstallTest"));
  auto remove = reinterpret_cast<int (*)(int)>(GetProcAddress(module, "RemoveTest"));
  auto reactivate = reinterpret_cast<int (*)()>(GetProcAddress(module, "ReactivateTest"));
  auto engine_values = reinterpret_cast<int (*)(float*, float*, uint8_t*, float*, int)>(GetProcAddress(module, "EngineValues"));
  CHECK(install && remove && reactivate && engine_values);
  CHECK(engine_values(&scaled_value, &reset_value, &flag_value, &constant_value, 0) == 0);
  CHECK(install(reinterpret_cast<void*>(distance_call), reinterpret_cast<void*>(&Scrub)) == 1);
  invoke_distance(nullptr, nullptr, out.data(), 0, 0, 0, 0);
  CHECK(out[0] == 20.f);
  CHECK(remove(stage) == (stage == 0 ? 0 : 2));
  CHECK(FreeLibrary(module));
  CHECK((GetModuleHandleW(L"native_hooks_test.dll") != nullptr) == (stage != 0));
  // Both calls remain valid after the host releases its module reference. The
  // scrub callback must bypass before dereferencing this deliberately null model.
  invoke_distance(nullptr, nullptr, out.data(), 0, 0, 0, 0);
  CHECK(out[0] == 10.f);
  invoke_scrub(nullptr, nullptr, 0, nullptr, 0, 0, 0);
  module = LoadLibraryW(L"native_hooks_test.dll");
  CHECK(module);
  install = reinterpret_cast<int (*)(void*, void*)>(GetProcAddress(module, "InstallTest"));
  remove = reinterpret_cast<int (*)(int)>(GetProcAddress(module, "RemoveTest"));
  reactivate = reinterpret_cast<int (*)()>(GetProcAddress(module, "ReactivateTest"));
  engine_values = reinterpret_cast<int (*)(float*, float*, uint8_t*, float*, int)>(GetProcAddress(module, "EngineValues"));
  CHECK(engine_values && scaled_value == 20.f && flag_value == 1 && constant_value == 10.f);
  CHECK(engine_values(&scaled_value, &reset_value, &flag_value, &constant_value, 2) == 0);
  CHECK(engine_values(&scaled_value, &reset_value, &flag_value, &constant_value, 1) == 0);
  if (stage != 0)
    CHECK(reactivate());
  else
    CHECK(install(reinterpret_cast<void*>(distance_call), reinterpret_cast<void*>(&Scrub)) == 1);
  CHECK(remove(0) == 0);
  CHECK(FreeLibrary(module));
  CHECK((GetModuleHandleW(L"native_hooks_test.dll") != nullptr) == (stage != 0));
#if !defined(_WIN64)
  module = LoadLibraryW(L"native_hooks_test.dll");
  CHECK(module);
  auto install_lod = reinterpret_cast<int (*)(void*)>(GetProcAddress(module, "InstallLod"));
  auto remove_lod = reinterpret_cast<int (*)(int)>(GetProcAddress(module, "RemoveLod"));
  CHECK(install_lod && remove_lod && install_lod(reinterpret_cast<void*>(&Lod)));
  CHECK(invoke_lod(nullptr, nullptr, 10.f, nullptr) == reinterpret_cast<void*>(20));
  CHECK(remove_lod(stage) == (stage == 0 ? 0 : 1));
  CHECK(FreeLibrary(module));
  CHECK(invoke_lod(nullptr, nullptr, 10.f, nullptr) == reinterpret_cast<void*>(10));
  if (stage != 0) {
    module = LoadLibraryW(L"native_hooks_test.dll");
    CHECK(module && remove_lod(0) == 0);
    CHECK(FreeLibrary(module));
  }
  // The LOD pick hook by the Model Detail Distance slider (Update's first look at the module, then every present), on a
  // PickLod made of the game's own first bytes that the add-on's pattern finds in the test module.
  module = LoadLibraryW(L"native_hooks_test.dll");
  CHECK(module);
  const auto fake_address = reinterpret_cast<const uint8_t* (*)()>(GetProcAddress(module, "FakePickLod"));
  const auto update_lod = reinterpret_cast<int (*)(float, const char**)>(GetProcAddress(module, "UpdateLod"));
  const auto detach_lod = reinterpret_cast<int (*)()>(GetProcAddress(module, "DetachLod"));
  install_lod = reinterpret_cast<int (*)(void*)>(GetProcAddress(module, "InstallLod"));
  remove_lod = reinterpret_cast<int (*)(int)>(GetProcAddress(module, "RemoveLod"));
  CHECK(fake_address && update_lod && detach_lod && install_lod && remove_lod);
  const uint8_t* const code = fake_address();
  using PickLod = void*(__fastcall*)(void*, void*, float, void**);
  const auto pick_lod = reinterpret_cast<PickLod>(const_cast<uint8_t*>(code));
  std::array<uint8_t, 16> game_bytes;
  std::memcpy(game_bytes.data(), code, game_bytes.size());
  std::array<uint32_t, 16> model = {};  // no LODs: the fake returns the distance's bits
  void* next = nullptr;
  const auto bits = [](float distance) {
    uint32_t value;
    std::memcpy(&value, &distance, sizeof(value));
    return reinterpret_cast<void*>(static_cast<uintptr_t>(value));
  };
  const auto game_bytes_still = [&] { return std::memcmp(code, game_bytes.data(), game_bytes.size()) == 0; };
  const char* status = nullptr;
  CHECK(pick_lod(model.data(), nullptr, 10.f, &next) == bits(10.f));
  // CONTROL: a hook as the build before installed it at every slider value, 1x too: the game's bytes change, which the
  // checks below read.
  CHECK(install_lod(const_cast<uint8_t*>(code)) && !game_bytes_still());
  CHECK(pick_lod(model.data(), nullptr, 10.f, &next) == bits(20.f));
  CHECK(remove_lod(0) == 0 && game_bytes_still());
  // 1x at the first look: the game's own PickLod, no detour, and the status says so.
  CHECK(update_lod(1.f, &status) == 0);
  CHECK(std::strstr(status, "at 1x: the game's LOD pick left alone") != nullptr && std::strstr(status, "not found") == nullptr);
  CHECK(game_bytes_still() && pick_lod(model.data(), nullptr, 10.f, &next) == bits(10.f));
  // The slider leaves 1x without a restart: still no detour, the note asks for the restart; back at 1x it is gone.
  CHECK(update_lod(2.f, &status) == 4);
  CHECK(update_lod(3.f, &status) == 4);
  CHECK(game_bytes_still() && pick_lod(model.data(), nullptr, 10.f, &next) == bits(10.f));
  CHECK(update_lod(1.f, &status) == 0);
  // A restart (the detach, then a new image's first look) with the slider up: hooked once, the camera's distance squared
  // divided by the slider squared as before; no note.
  CHECK(detach_lod() == 0 && game_bytes_still());
  CHECK(update_lod(2.f, &status) == 3);
  CHECK(std::strstr(status, "object detail hooked") != nullptr && !game_bytes_still() && code[0] == 0xE9);
  CHECK(pick_lod(model.data(), nullptr, 10.f, &next) == bits(10.f * (1.f / (2.f * 2.f))));
  CHECK(update_lod(3.f, &status) == 3);
  CHECK(pick_lod(model.data(), nullptr, 10.f, &next) == bits(10.f * (1.f / (3.f * 3.f))));
  // Back at 1x the hook stays in and passes the distance through; up again, live.
  CHECK(update_lod(1.f, &status) == 3);
  CHECK(pick_lod(model.data(), nullptr, 10.f, &next) == bits(10.f));
  CHECK(update_lod(8.f, &status) == 3);
  CHECK(pick_lod(model.data(), nullptr, 10.f, &next) == bits(10.f * (1.f / (8.f * 8.f))));
  // The detach removes it: the game's bytes and results again.
  CHECK(detach_lod() == 0 && game_bytes_still());
  CHECK(pick_lod(model.data(), nullptr, 10.f, &next) == bits(10.f));
  CHECK(FreeLibrary(module));
  std::printf("PASS stage %d: LOD pick: no hook at 1x, hooked once when the game starts with the slider up, same results as before\n", stage);
#endif
  std::printf("PASS stage %d: actual callbacks/Detours, host unload, retained records, retry and reload, engine values kept across an image that saw no frame\n", stage);
}
