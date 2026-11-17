// A second RenoDX module built with the bind cache (tests.cpp's threaded parts): its own utils::shader / pipeline over the
// same process-shared pipeline map and bind cache block. The test exe attached first, so it handles the events and binds;
// the partner's writes (destroy, init at the same handle, a new replacement) come from this module's code. Its ReShade
// calls reach the test exe's mocked exports.
#include "test/tw_pipeline_cache/common.hpp"

namespace test = pipeline_cache_test;

extern "C" __declspec(dllexport) void PartnerAttach() {
  renodx::utils::shader::Use(DLL_PROCESS_ATTACH);
}

extern "C" __declspec(dllexport) void PartnerDetach() {
  renodx::utils::shader::Use(DLL_PROCESS_DETACH);
}

extern "C" __declspec(dllexport) void PartnerChurn(test::ChurnControl* control) {
  test::Churn(control);
}
