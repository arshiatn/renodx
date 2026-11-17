// An add-on built before the bind cache (tests.cpp's old-module parts): attached to the pipeline map (pipeline::shared's
// module record) without a token in the bind cache's block, and writing a replacement without bumping the generation.
#include "src/utils/pipeline.hpp"

namespace pipeline = renodx::utils::pipeline;

// Its events as an old pipeline::Use registers them (first in the process, it handles them), without the token.
extern "C" __declspec(dllexport) void OldAttach() {
  pipeline::internal::shared.RegisterModule();
  pipeline::internal::shared.RegisterEvent<reshade::addon_event::init_pipeline>(pipeline::OnInitPipeline);
  pipeline::internal::shared.RegisterEvent<reshade::addon_event::bind_pipeline>(pipeline::OnBindPipeline);
  pipeline::internal::shared.RegisterEvent<reshade::addon_event::destroy_pipeline>(pipeline::OnDestroyPipeline);
}

extern "C" __declspec(dllexport) void OldDetach() {
  pipeline::internal::shared.UnregisterEvent<reshade::addon_event::init_pipeline>(pipeline::OnInitPipeline);
  pipeline::internal::shared.UnregisterEvent<reshade::addon_event::bind_pipeline>(pipeline::OnBindPipeline);
  pipeline::internal::shared.UnregisterEvent<reshade::addon_event::destroy_pipeline>(pipeline::OnDestroyPipeline);
  pipeline::internal::shared.UnregisterModule();
}

extern "C" __declspec(dllexport) void OldSetReplacement(uint64_t handle, uint64_t replacement) {
  pipeline::internal::shared.data->pipeline_infos.modify_if(handle, [&](std::pair<const uint64_t, pipeline::PipelineInfo>& pair) {
    pair.second.details.replacement_pipeline = {replacement};
  });
}
