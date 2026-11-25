/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// zz_capture.addon64: the frame as it goes to the display. Its present callback runs after RenoDX's (add-ons register
// in load order, and this file's name sorts after renodx-*), so it sees the swap chain proxy's output, and before
// ReShade's own runtime and GUI draw into the back buffer (the present event comes first).
// RENODX_CAPTURE_EVERY=1: every present. RENODX_CAPTURE_AT=<s>[,<s>...]: once at each second after the first present.
// Each capture: capture_<present>.raw in RENODX_CAPTURE_DIR (else the exe's folder), a Header then tight rows.

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <include/reshade.hpp>

extern "C" __declspec(dllexport) const char* NAME = "zz_capture";
extern "C" __declspec(dllexport) const char* DESCRIPTION = "Writes the swap chain's back buffer after the add-ons' present work.";

namespace {

struct Header {
  char magic[8] = {'R', 'D', 'X', 'C', 'A', 'P', '1', 0};
  uint32_t format = 0;  // reshade::api::format
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t row_pitch = 0;  // bytes of one row in the file
  uint32_t present = 0;
  uint32_t api = 0;  // reshade::api::device_api
};

struct Staging {
  reshade::api::device* device = nullptr;
  reshade::api::resource resource = {0};
  reshade::api::resource_desc desc = {};
};

Staging staging;
uint32_t present_count = 0;
std::optional<std::chrono::steady_clock::time_point> first_present;
std::vector<double> capture_seconds;
bool capture_every = false;
std::filesystem::path capture_dir;

void ReleaseStaging() {
  if (staging.device != nullptr && staging.resource.handle != 0u) {
    staging.device->destroy_resource(staging.resource);
  }
  staging = {};
}

void OnDestroyDevice(reshade::api::device* device) {
  if (device == staging.device) {
    ReleaseStaging();
  }
}

void OnPresent(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain, const reshade::api::rect*,
               const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  const uint32_t present = ++present_count;
  const auto now = std::chrono::steady_clock::now();
  if (!first_present.has_value()) {
    first_present = now;
  }
  bool capture = capture_every;
  const double seconds = std::chrono::duration<double>(now - *first_present).count();
  while (!capture_seconds.empty() && seconds >= capture_seconds.front()) {
    capture_seconds.erase(capture_seconds.begin());
    capture = true;
  }
  if (!capture) return;

  auto* device = swapchain->get_device();
  const reshade::api::resource back_buffer = swapchain->get_current_back_buffer();
  const reshade::api::resource_desc back_desc = device->get_resource_desc(back_buffer);
  if (staging.device != device || staging.desc.texture.width != back_desc.texture.width
      || staging.desc.texture.height != back_desc.texture.height || staging.desc.texture.format != back_desc.texture.format) {
    ReleaseStaging();
    staging.desc = reshade::api::resource_desc(back_desc.texture.width, back_desc.texture.height, 1, 1, back_desc.texture.format, 1,
                                               reshade::api::memory_heap::gpu_to_cpu, reshade::api::resource_usage::copy_dest);
    if (!device->create_resource(staging.desc, nullptr, reshade::api::resource_usage::copy_dest, &staging.resource)) {
      reshade::log::message(reshade::log::level::error, "zz_capture: no staging texture");
      staging = {};
      return;
    }
    staging.device = device;
  }

  auto* cmd_list = queue->get_immediate_command_list();
  cmd_list->barrier(back_buffer, reshade::api::resource_usage::present, reshade::api::resource_usage::copy_source);
  cmd_list->copy_texture_region(back_buffer, 0, nullptr, staging.resource, 0, nullptr);
  cmd_list->barrier(back_buffer, reshade::api::resource_usage::copy_source, reshade::api::resource_usage::present);
  queue->flush_immediate_command_list();
  queue->wait_idle();

  reshade::api::subresource_data mapped = {};
  if (!device->map_texture_region(staging.resource, 0, nullptr, reshade::api::map_access::read_only, &mapped)) {
    reshade::log::message(reshade::log::level::error, "zz_capture: map failed");
    return;
  }
  const Header header = {
      .format = static_cast<uint32_t>(back_desc.texture.format),
      .width = back_desc.texture.width,
      .height = back_desc.texture.height,
      .row_pitch = reshade::api::format_row_pitch(back_desc.texture.format, back_desc.texture.width),
      .present = present,
      .api = static_cast<uint32_t>(device->get_api()),
  };
  std::ofstream file(capture_dir / std::format("capture_{:05}.raw", present), std::ios::binary);
  file.write(reinterpret_cast<const char*>(&header), sizeof(header));
  for (uint32_t row = 0; row < header.height; ++row) {
    file.write(static_cast<const char*>(mapped.data) + (static_cast<size_t>(row) * mapped.row_pitch), header.row_pitch);
  }
  device->unmap_texture_region(staging.resource, 0);
}

void ReadArming() {
  char value[512] = {};
  capture_every = (GetEnvironmentVariableA("RENODX_CAPTURE_EVERY", value, sizeof(value)) != 0 && value[0] == '1');
  if (GetEnvironmentVariableA("RENODX_CAPTURE_AT", value, sizeof(value)) != 0) {
    char* next = nullptr;
    for (char* token = strtok_s(value, ",", &next); token != nullptr; token = strtok_s(nullptr, ",", &next)) {
      capture_seconds.push_back(std::strtod(token, nullptr));
    }
  }
  wchar_t dir[MAX_PATH] = {};
  if (GetEnvironmentVariableW(L"RENODX_CAPTURE_DIR", dir, MAX_PATH) != 0) {
    capture_dir = dir;
  } else {
    GetModuleFileNameW(nullptr, dir, MAX_PATH);
    capture_dir = std::filesystem::path(dir).parent_path();
  }
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
  switch (reason) {
    case DLL_PROCESS_ATTACH:
      if (!reshade::register_addon(module)) return FALSE;
      ReadArming();
      reshade::register_event<reshade::addon_event::present>(OnPresent);
      reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
      break;
    case DLL_PROCESS_DETACH:
      reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
      reshade::unregister_event<reshade::addon_event::present>(OnPresent);
      reshade::unregister_addon(module);
      break;
  }
  return TRUE;
}
