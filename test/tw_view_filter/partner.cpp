// A second RenoDX module built with the view filter (tests.cpp's threaded, interest and slot parts): its own resource /
// resource_upgrade over the same process-shared view map and filter block, loaded after the test's threads started.
// Its ReShade calls reach the test exe's mocked exports.
#include "test/tw_view_filter/common.hpp"

namespace test = view_filter_test;

extern "C" __declspec(dllexport) void PartnerAttach(bool read) {
  test::res::use_view_filter = read;
  test::upgrade::Use(DLL_PROCESS_ATTACH);
}

extern "C" __declspec(dllexport) void PartnerDetach() {
  test::upgrade::Use(DLL_PROCESS_DETACH);
}

extern "C" __declspec(dllexport) bool PartnerReading() {
  return test::res::view_filter_reading.load();
}

extern "C" __declspec(dllexport) void PartnerStress(test::StressControl* control, int role) {
  test::Stress(control, role);
}

// This module's GetResourceViewClone against the oracle, for every handle given (no writer running): wrong answers.
extern "C" __declspec(dllexport) uint64_t PartnerVerify(const uint64_t* handles, size_t count) {
  test::Counts counts;
  for (size_t index = 0u; index < count; ++index) {
    for (const auto& options : test::ALL_OPTIONS) {
      if (!options.allow_create) test::Lookup(handles[index], options, &counts);
    }
  }
  return counts.wrong;
}

extern "C" __declspec(dllexport) void PartnerCheckCounts(uint32_t* checked, uint32_t* races, uint32_t* mismatches) {
  *checked = test::upgrade::view_filter_checked.load();
  *races = test::upgrade::view_filter_races.load();
  *mismatches = test::upgrade::view_filter_mismatches.load();
}
