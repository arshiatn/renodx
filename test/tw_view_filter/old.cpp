// An add-on built before the view filter (tests.cpp's old-module parts): attached to the view map (resource::shared's
// module record) without a token in the filter block, and writing clone_enabled without setting the view's bit.
#include "src/utils/resource.hpp"

namespace resource = renodx::utils::resource;

extern "C" __declspec(dllexport) void OldAttach() {
  resource::shared.RegisterModule();
}

extern "C" __declspec(dllexport) void OldDetach() {
  resource::shared.UnregisterModule();
}

extern "C" __declspec(dllexport) void OldEnable(uint64_t handle) {
  resource::shared.data->resource_view_infos.modify_if(handle, [](std::pair<const uint64_t, resource::ResourceViewInfo>& pair) {
    pair.second.clone_enabled = true;
  });
}
