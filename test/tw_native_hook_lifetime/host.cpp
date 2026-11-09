#include <windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#define CHECK(x)                                                        \
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
  CHECK(install && remove && reactivate);
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
#endif
  std::printf("PASS stage %d: actual callbacks/Detours, host unload, retained records, retry and reload\n", stage);
}
