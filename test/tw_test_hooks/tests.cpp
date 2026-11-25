// twattila/test_hooks.hpp's runtime filter: a D3D9 game behind the D3D11 proxy has a runtime for each device, and only
// the proxy's answers RENODX_TW_SET / SHOT / PROFILE. Without a proxy (every D3D11 game) or with a proxy that could not be
// made, the game's own runtime answers as before. The hooks read their environment once: one mode per run (proxy,
// noproxy, failed). `control`: the proxy's expectations without a proxy, so the game's runtime answers; it must fail.
// A button by its label presses every button with it, by "<group>/<label>" only that group's.
// RENODX_TW_SET_FILE (modes setfile, bothset, nohooks, nofile, bom, utf16): the entries from a file (CRLF, one per line,
// past SET's 8 KB, a second entry on a line warned about and not used), the live "env." and "mt_trace" keys through the
// add-on's LiveHooks, and the refusals: both lists set, no hooks, a file that cannot be read, a UTF-8 byte order mark or
// a UTF-16 file (one error, no entries).
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

static int profiles = 0;
static void CountProfile() { ++profiles; }
#define RENODX_TEST_HOOKS_PROFILE CountProfile
#include "src/games/twattila/test_hooks.hpp"

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)
#define UNEXPECTED { throw std::logic_error("unexpected mock call"); }

extern "C" __declspec(dllexport) const char* const NAME = "tw_test_hooks";

static std::map<std::pair<std::string, std::string>, std::string> ini;
static std::vector<std::string> logged;
static bool print_logs = true;
static void* registered_present = nullptr;

extern "C" {
__declspec(dllexport) bool ReShadeRegisterAddon(void*, uint32_t) { return true; }
__declspec(dllexport) void ReShadeUnregisterAddon(void*) {}
__declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) {
  logged.emplace_back(message);
  if (print_logs) {
    std::printf("  log: %s\n", message);
  }
}
__declspec(dllexport) void ReShadeRegisterEvent(reshade::addon_event event, void* callback) {
  if (event == reshade::addon_event::reshade_present) {
    registered_present = callback;
  }
}
__declspec(dllexport) void ReShadeUnregisterEvent(reshade::addon_event event, void* callback) {
  if (event == reshade::addon_event::reshade_present && callback == registered_present) {
    registered_present = nullptr;
  }
}
__declspec(dllexport) bool ReShadeGetConfigValue(void*, void*, const char* section, const char* key, char* value, size_t* size) {
  const auto found = ini.find({section, key});
  if (found == ini.end()) return false;
  const size_t needed = found->second.size() + 1;
  if (value != nullptr && *size > 0) {
    const size_t copied = std::min(needed, *size) - 1;
    std::memcpy(value, found->second.data(), copied);
    value[copied] = '\0';
  }
  *size = needed;
  return true;
}
__declspec(dllexport) void ReShadeSetConfigValue(void*, void*, const char* section, const char* key, const char* value) {
  ini[{section, key}] = value;
}
}

namespace mock {
using namespace reshade::api;

// IsProxyDevice asks a device that is not the proxy's for its native pointer only.
struct Device : device {
  uint64_t native = 0;
  uint64_t get_native() const override { return native; }
  void get_private_data(const uint8_t guid[16], uint64_t* data) const override UNEXPECTED
  void set_private_data(const uint8_t guid[16], const uint64_t data) override UNEXPECTED
  device_api get_api() const override UNEXPECTED
  bool check_capability(device_caps capability) const override UNEXPECTED
  bool check_format_support(format format, resource_usage usage) const override UNEXPECTED
  bool create_sampler(const sampler_desc& desc, sampler* out_sampler) override UNEXPECTED
  void destroy_sampler(sampler sampler) override UNEXPECTED
  bool create_resource(const resource_desc& desc, const subresource_data* initial_data, resource_usage initial_state, resource* out_resource, void** shared_handle = nullptr) override UNEXPECTED
  void destroy_resource(resource resource) override UNEXPECTED
  resource_desc get_resource_desc(resource resource) const override UNEXPECTED
  bool create_resource_view(resource resource, resource_usage usage_type, const resource_view_desc& desc, resource_view* out_view) override UNEXPECTED
  void destroy_resource_view(resource_view view) override UNEXPECTED
  resource get_resource_from_view(resource_view view) const override UNEXPECTED
  resource_view_desc get_resource_view_desc(resource_view view) const override UNEXPECTED
  bool map_buffer_region(resource resource, uint64_t offset, uint64_t size, map_access access, void** out_data) override UNEXPECTED
  void unmap_buffer_region(resource resource) override UNEXPECTED
  bool map_texture_region(resource resource, uint32_t subresource, const subresource_box* box, map_access access, subresource_data* out_data) override UNEXPECTED
  void unmap_texture_region(resource resource, uint32_t subresource) override UNEXPECTED
  void update_buffer_region(const void* data, resource dest, uint64_t dest_offset, uint64_t size) override UNEXPECTED
  void update_texture_region(const subresource_data& data, resource dest, uint32_t dest_subresource, const subresource_box* dest_box = nullptr) override UNEXPECTED
  bool create_pipeline(pipeline_layout layout, uint32_t subobject_count, const pipeline_subobject* subobjects, pipeline* out_pipeline) override UNEXPECTED
  void destroy_pipeline(pipeline pipeline) override UNEXPECTED
  bool create_pipeline_layout(uint32_t param_count, const pipeline_layout_param* params, pipeline_layout* out_layout) override UNEXPECTED
  void destroy_pipeline_layout(pipeline_layout layout) override UNEXPECTED
  bool allocate_descriptor_tables(uint32_t count, pipeline_layout layout, uint32_t param, descriptor_table* out_tables) override UNEXPECTED
  void free_descriptor_tables(uint32_t count, const descriptor_table* tables) override UNEXPECTED
  void get_descriptor_heap_offset(descriptor_table table, uint32_t binding, uint32_t array_offset, descriptor_heap* out_heap, uint32_t* out_offset) const override UNEXPECTED
  void copy_descriptor_tables(uint32_t count, const descriptor_table_copy* copies) override UNEXPECTED
  void update_descriptor_tables(uint32_t count, const descriptor_table_update* updates) override UNEXPECTED
  bool create_query_heap(query_type type, uint32_t count, query_heap* out_heap) override UNEXPECTED
  void destroy_query_heap(query_heap heap) override UNEXPECTED
  bool get_query_heap_results(query_heap heap, uint32_t first, uint32_t count, void* results, uint32_t stride) override UNEXPECTED
  void set_resource_name(resource resource, const char* name) override UNEXPECTED
  void set_resource_view_name(resource_view view, const char* name) override UNEXPECTED
  bool create_fence(uint64_t initial_value, fence_flags flags, fence* out_fence, void** shared_handle = nullptr) override UNEXPECTED
  void destroy_fence(fence fence) override UNEXPECTED
  uint64_t get_completed_fence_value(fence fence) const override UNEXPECTED
  bool wait(fence fence, uint64_t value, uint64_t timeout = UINT64_MAX) override UNEXPECTED
  bool signal(fence fence, uint64_t value) override UNEXPECTED
  bool get_property(device_properties property, void* data) const override UNEXPECTED
  uint64_t get_resource_view_gpu_address(resource_view view) const override UNEXPECTED
  void get_acceleration_structure_size(acceleration_structure_type type, acceleration_structure_build_flags flags, uint32_t input_count, const acceleration_structure_build_input* inputs, uint64_t* out_size, uint64_t* out_build_scratch_size, uint64_t* out_update_scratch_size) const override UNEXPECTED
  bool get_pipeline_shader_group_handles(pipeline pipeline, uint32_t first, uint32_t count, void* out_handles) override UNEXPECTED
};

// The hooks use a runtime's device and its screenshot only.
struct Runtime : effect_runtime {
  device* owner = nullptr;
  int screenshots = 0;
  device* get_device() override { return owner; }
  void save_screenshot(const char* postfix = nullptr) override { ++screenshots; }
  uint64_t get_native() const override UNEXPECTED
  void get_private_data(const uint8_t guid[16], uint64_t* data) const override UNEXPECTED
  void set_private_data(const uint8_t guid[16], const uint64_t data) override UNEXPECTED
  void* get_hwnd() const override UNEXPECTED
  resource get_back_buffer(uint32_t index) override UNEXPECTED
  uint32_t get_back_buffer_count() const override UNEXPECTED
  uint32_t get_current_back_buffer_index() const override UNEXPECTED
  command_queue* get_command_queue() override UNEXPECTED
  void render_effects(command_list* cmd_list, resource_view rtv, resource_view rtv_srgb) override UNEXPECTED
  bool capture_screenshot(void* pixels) override UNEXPECTED
  void get_screenshot_width_and_height(uint32_t* out_width, uint32_t* out_height) const override UNEXPECTED
  bool is_key_down(uint32_t keycode) const override UNEXPECTED
  bool is_key_pressed(uint32_t keycode) const override UNEXPECTED
  bool is_key_released(uint32_t keycode) const override UNEXPECTED
  bool is_mouse_button_down(uint32_t button) const override UNEXPECTED
  bool is_mouse_button_pressed(uint32_t button) const override UNEXPECTED
  bool is_mouse_button_released(uint32_t button) const override UNEXPECTED
  void get_mouse_cursor_position(uint32_t* out_x, uint32_t* out_y, int16_t* out_wheel_delta = nullptr) const override UNEXPECTED
  void enumerate_uniform_variables(const char* effect_name, void (*callback)(effect_runtime* runtime, effect_uniform_variable variable, void* user_data), void* user_data) override UNEXPECTED
  effect_uniform_variable find_uniform_variable(const char* effect_name, const char* variable_name) const override UNEXPECTED
  void get_uniform_variable_type(effect_uniform_variable variable, format* out_base_type, uint32_t* out_rows = nullptr, uint32_t* out_columns = nullptr, uint32_t* out_array_length = nullptr) const override UNEXPECTED
  void get_uniform_variable_name(effect_uniform_variable variable, char* name, size_t* name_size) const override UNEXPECTED
  bool get_annotation_bool_from_uniform_variable(effect_uniform_variable variable, const char* name, bool* values, size_t count, size_t array_index = 0) const override UNEXPECTED
  bool get_annotation_float_from_uniform_variable(effect_uniform_variable variable, const char* name, float* values, size_t count, size_t array_index = 0) const override UNEXPECTED
  bool get_annotation_int_from_uniform_variable(effect_uniform_variable variable, const char* name, int32_t* values, size_t count, size_t array_index = 0) const override UNEXPECTED
  bool get_annotation_uint_from_uniform_variable(effect_uniform_variable variable, const char* name, uint32_t* values, size_t count, size_t array_index = 0) const override UNEXPECTED
  bool get_annotation_string_from_uniform_variable(effect_uniform_variable variable, const char* name, char* value, size_t* value_size) const override UNEXPECTED
  void get_uniform_value_bool(effect_uniform_variable variable, bool* values, size_t count, size_t array_index = 0) const override UNEXPECTED
  void get_uniform_value_float(effect_uniform_variable variable, float* values, size_t count, size_t array_index = 0) const override UNEXPECTED
  void get_uniform_value_int(effect_uniform_variable variable, int32_t* values, size_t count, size_t array_index = 0) const override UNEXPECTED
  void get_uniform_value_uint(effect_uniform_variable variable, uint32_t* values, size_t count, size_t array_index = 0) const override UNEXPECTED
  void set_uniform_value_bool(effect_uniform_variable variable, const bool* values, size_t count, size_t array_index = 0) override UNEXPECTED
  void set_uniform_value_float(effect_uniform_variable variable, const float* values, size_t count, size_t array_index = 0) override UNEXPECTED
  void set_uniform_value_int(effect_uniform_variable variable, const int32_t* values, size_t count, size_t array_index = 0) override UNEXPECTED
  void set_uniform_value_uint(effect_uniform_variable variable, const uint32_t* values, size_t count, size_t array_index = 0) override UNEXPECTED
  void enumerate_texture_variables(const char* effect_name, void (*callback)(effect_runtime* runtime, effect_texture_variable variable, void* user_data), void* user_data) override UNEXPECTED
  effect_texture_variable find_texture_variable(const char* effect_name, const char* variable_name) const override UNEXPECTED
  void get_texture_variable_name(effect_texture_variable variable, char* name, size_t* name_size) const override UNEXPECTED
  bool get_annotation_bool_from_texture_variable(effect_texture_variable variable, const char* name, bool* values, size_t count, size_t array_index = 0) const override UNEXPECTED
  bool get_annotation_float_from_texture_variable(effect_texture_variable variable, const char* name, float* values, size_t count, size_t array_index = 0) const override UNEXPECTED
  bool get_annotation_int_from_texture_variable(effect_texture_variable variable, const char* name, int32_t* values, size_t count, size_t array_index = 0) const override UNEXPECTED
  bool get_annotation_uint_from_texture_variable(effect_texture_variable variable, const char* name, uint32_t* values, size_t count, size_t array_index = 0) const override UNEXPECTED
  bool get_annotation_string_from_texture_variable(effect_texture_variable variable, const char* name, char* value, size_t* value_size) const override UNEXPECTED
  void update_texture(effect_texture_variable variable, const uint32_t width, const uint32_t height, const void* pixels) override UNEXPECTED
  void get_texture_binding(effect_texture_variable variable, resource_view* out_srv, resource_view* out_srv_srgb) const override UNEXPECTED
  void update_texture_bindings(const char* semantic, resource_view srv, resource_view srv_srgb) override UNEXPECTED
  void enumerate_techniques(const char* effect_name, void (*callback)(effect_runtime* runtime, effect_technique technique, void* user_data), void* user_data) override UNEXPECTED
  effect_technique find_technique(const char* effect_name, const char* technique_name) override UNEXPECTED
  void get_technique_name(effect_technique technique, char* name, size_t* name_size) const override UNEXPECTED
  bool get_annotation_bool_from_technique(effect_technique technique, const char* name, bool* values, size_t count, size_t array_index = 0) const override UNEXPECTED
  bool get_annotation_float_from_technique(effect_technique technique, const char* name, float* values, size_t count, size_t array_index = 0) const override UNEXPECTED
  bool get_annotation_int_from_technique(effect_technique technique, const char* name, int32_t* values, size_t count, size_t array_index = 0) const override UNEXPECTED
  bool get_annotation_uint_from_technique(effect_technique technique, const char* name, uint32_t* values, size_t count, size_t array_index = 0) const override UNEXPECTED
  bool get_annotation_string_from_technique(effect_technique technique, const char* name, char* value, size_t* value_size) const override UNEXPECTED
  bool get_technique_state(effect_technique technique) const override UNEXPECTED
  void set_technique_state(effect_technique technique, bool enabled) override UNEXPECTED
  bool get_preprocessor_definition(const char* name, char* value, size_t* value_size) const override UNEXPECTED
  void set_preprocessor_definition(const char* name, const char* value) override UNEXPECTED
  void render_technique(effect_technique technique, command_list* cmd_list, resource_view rtv, resource_view rtv_srgb = {0}) override UNEXPECTED
  bool get_effects_state() const override UNEXPECTED
  void set_effects_state(bool enabled) override UNEXPECTED
  void get_current_preset_path(char* path, size_t* path_size) const override UNEXPECTED
  void set_current_preset_path(const char* path) override UNEXPECTED
  void reorder_techniques(size_t count, const effect_technique* techniques) override UNEXPECTED
  void block_input_next_frame() override UNEXPECTED
  uint32_t last_key_pressed() const override UNEXPECTED
  uint32_t last_key_released() const override UNEXPECTED
  void get_uniform_variable_effect_name(effect_uniform_variable variable, char* effect_name, size_t* effect_name_size) const override UNEXPECTED
  void get_texture_variable_effect_name(effect_texture_variable variable, char* effect_name, size_t* effect_name_size) const override UNEXPECTED
  void get_technique_effect_name(effect_technique technique, char* effect_name, size_t* effect_name_size) const override UNEXPECTED
  void save_current_preset() const override UNEXPECTED
  bool get_preprocessor_definition_for_effect(const char* effect_name, const char* name, char* value, size_t* value_size) const override UNEXPECTED
  void set_preprocessor_definition_for_effect(const char* effect_name, const char* name, const char* value) override UNEXPECTED
  bool open_overlay(bool open, input_source source) override UNEXPECTED
  void set_color_space(color_space color_space) override UNEXPECTED
  void reset_uniform_value(effect_uniform_variable variable) override UNEXPECTED
  void reload_effect_next_frame(const char* effect_name) override UNEXPECTED
  void export_current_preset(const char* path) const override UNEXPECTED
};
}  // namespace mock

namespace hooks = twattila::test_hooks;
namespace proxy = renodx::utils::device_proxy;

static float bloom = 1.f;
// Two buttons with one label (every page's Reset): "Reset" presses both, "page-presets/Reset" only its group's.
static int page_resets = 0;
static int ao_resets = 0;
static renodx::utils::settings::Settings rows = {
    new renodx::utils::settings::Setting{.key = "bloom", .binding = &bloom, .default_value = 1.f, .max = 2.f},
    new renodx::utils::settings::Setting{.value_type = renodx::utils::settings::SettingValueType::BUTTON, .label = "Reset",
                                         .group = "page-presets", .on_change = [] { ++page_resets; }},
    new renodx::utils::settings::Setting{.value_type = renodx::utils::settings::SettingValueType::BUTTON, .label = "Reset",
                                         .group = "ao-presets", .on_change = [] { ++ao_resets; }},
};

static uint32_t env_generation = 0;
static std::vector<uint32_t> armed;

static int RunSetFile(const std::string& mode) {
  const auto count = [](const std::string& line) { return std::count(logged.begin(), logged.end(), line); };
  const auto env = [](const char* name) {
    std::string value(256, '\0');
    const DWORD length = GetEnvironmentVariableA(name, value.data(), static_cast<DWORD>(value.size()));
    return (length == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) ? std::string("<unset>") : value.substr(0, length);
  };
  // Next to the test exe (the test folder), never in the repository.
  std::string path(MAX_PATH, '\0');
  path.resize(GetModuleFileNameA(nullptr, path.data(), static_cast<DWORD>(path.size())));
  path = path.substr(0, path.find_last_of('\\') + 1) + ((mode == "nofile") ? "missing_set_file.txt" : "set_file.txt");
  std::remove(path.c_str());
  // A marker an earlier mode or run left would make the line read "over one not read yet".
  std::filesystem::remove(std::filesystem::path(path).parent_path() / L"renodx_pass_dump_now");
  if (mode != "nofile") {
    // Blanks after an entry's ';' are no second entry; line 806's second entry is warned about and not used.
    std::string text = "0:bloom=0.5; \r\n0:env.RENODX_TEST_ENV=1:4,2:2;\r\n0:mt_trace=20;\r\n0:pass_dump=2 c1;\r\n\r\n";
    // Past RENODX_TW_SET's 8 KB: every line is read.
    for (int index = 0; index < 800; ++index) {
      text += "0:bloom=0.5;\r\n";
    }
    text += "0:bloom=0.5;1:bloom=2;\r\n1:env.RENODX_TEST_ENV=;\r\n1:mt_trace=x;\r\n1:env.RENODX_TEST_NEVER_SET=;\r\n1:bloom=1.5";
    if (mode == "bom") {
      text.insert(0, "\xEF\xBB\xBF");
    } else if (mode == "utf16") {
      std::string wide = "\xFF\xFE";
      for (const char character : text) {
        wide += character;
        wide += '\0';
      }
      text = wide;
    }
    FILE* file = std::fopen(path.c_str(), "wb");
    CHECK(file != nullptr);
    std::fwrite(text.data(), 1, text.size(), file);
    std::fclose(file);
  }
  SetEnvironmentVariableA("RENODX_TW_SET_FILE", path.c_str());
  if (mode == "bothset") {
    SetEnvironmentVariableA("RENODX_TW_SET", "0:bloom=0.25");
  }
  print_logs = false;

  mock::Device device;
  device.native = 0x9000;
  mock::Runtime runtime;
  runtime.owner = &device;
  proxy::use_device_proxy = false;
  const hooks::LiveHooks live = {.env_changed = [] { return ++env_generation; }, .arm_trace = [](uint32_t frames) { armed.push_back(frames); }};
  hooks::Install(&rows, true, "[Test]", (mode == "nohooks") ? hooks::LiveHooks{} : live);
  // SET_FILE alone installs the hooks.
  CHECK(registered_present == reinterpret_cast<void*>(&hooks::internal::OnReshadePresent));
  hooks::internal::OnReshadePresent(&runtime);
  if (mode == "setfile") {
    CHECK(count("[Test] Test: RENODX_TW_SET_FILE " + path + " read (809 entries).") == 1);
    CHECK(count("[Test] Test: RENODX_TW_SET_FILE " + path + " line 806 has text after its entry's ';', which is not used.") == 1);
    CHECK(std::count_if(logged.begin(), logged.end(), [](const std::string& line) { return line.find("has text after") != std::string::npos; }) == 1);
    CHECK(bloom == 0.5f && count("[Test] Test: bloom set to 0.5.") == 802);
    CHECK(env("RENODX_TEST_ENV") == "1:4,2:2" && count("[Test] Test: env RENODX_TEST_ENV set to 1:4,2:2 (generation 1).") == 1);
    CHECK(armed.size() == 1 && armed[0] == 20 && count("[Test] Test: mt_trace armed 20 frames.") == 1);
    CHECK(env_generation == 1);
    // pass_dump: its value in renodx_pass_dump_now next to the exe (no temp file left).
    {
      std::ifstream marker(hooks::internal::exe_folder / L"renodx_pass_dump_now", std::ios::binary);
      const std::string written((std::istreambuf_iterator<char>(marker)), std::istreambuf_iterator<char>());
      marker.close();
      CHECK(written == "2 c1" && count("[Test] Test: renodx_pass_dump_now written (2 c1).") == 1);
      CHECK(!std::filesystem::exists(hooks::internal::exe_folder / L"renodx_pass_dump_now.tmp"));
      std::filesystem::remove(hooks::internal::exe_folder / L"renodx_pass_dump_now");
    }
    hooks::internal::clock_start.store(GetTickCount64() - 2000);
    hooks::internal::OnReshadePresent(&runtime);
    CHECK(env("RENODX_TEST_ENV") == "<unset>" && count("[Test] Test: env RENODX_TEST_ENV cleared (generation 2).") == 1);
    CHECK(armed.size() == 1 && count("[Test] Test: mt_trace=x is not a frame count, nothing armed.") == 1);
    CHECK(count("[Test] Test: env RENODX_TEST_NEVER_SET cleared (generation 3).") == 1 && env_generation == 3);
    CHECK(bloom == 1.5f);
  } else if (mode == "bothset") {
    CHECK(count("[Test] Test: RENODX_TW_SET and RENODX_TW_SET_FILE are both set; neither is used.") == 1);
    hooks::internal::clock_start.store(GetTickCount64() - 2000);
    hooks::internal::OnReshadePresent(&runtime);
    CHECK(bloom == 1.f && env_generation == 0 && armed.empty() && env("RENODX_TEST_ENV") == "<unset>");
  } else if (mode == "nohooks") {
    CHECK(bloom == 0.5f && env("RENODX_TEST_ENV") == "<unset>" && env_generation == 0 && armed.empty());
    CHECK(count("[Test] Test: env RENODX_TEST_ENV: no live env in this add-on, nothing changed.") == 1);
    CHECK(count("[Test] Test: mt_trace: no trace window in this add-on, nothing armed.") == 1);
  } else if (mode == "bom" || mode == "utf16") {
    CHECK(count("[Test] Test: RENODX_TW_SET_FILE " + path + " is not plain text (a byte order mark or UTF-16); no set entries.") == 1);
    hooks::internal::clock_start.store(GetTickCount64() - 2000);
    hooks::internal::OnReshadePresent(&runtime);
    CHECK(logged.size() == 1 && bloom == 1.f && env_generation == 0 && armed.empty() && env("RENODX_TEST_ENV") == "<unset>");
  } else {
    CHECK(count("[Test] Test: RENODX_TW_SET_FILE " + path + " could not be read; no set entries.") == 1 && bloom == 1.f);
  }
  hooks::Uninstall();
  CHECK(registered_present == nullptr);
  std::remove(path.c_str());
  std::filesystem::remove(hooks::internal::exe_folder / L"renodx_pass_dump_now");
  for (const auto& line : logged) {
    if (line.find(" set to 0.5.") == std::string::npos) {
      std::printf("  log: %s\n", line.c_str());
    }
  }
  std::printf("PASS %s: RENODX_TW_SET_FILE, env. and mt_trace keys\n", mode.c_str());
  return 0;
}

int main(int argc, char** argv) {
  const std::string mode = (argc > 1) ? argv[1] : "proxy";
  if (mode == "setfile" || mode == "bothset" || mode == "nohooks" || mode == "nofile" || mode == "bom" || mode == "utf16") return RunSetFile(mode);
  // A D3D9 game behind the proxy, the case the filter is for; the control expects it without the proxy.
  const bool expect_proxy = (mode == "proxy" || mode == "control");
  SetEnvironmentVariableA("RENODX_TW_SET", "0:bloom=0.5;0:shot=1;0:page-presets/Reset=1;0:Reset=1");
  SetEnvironmentVariableA("RENODX_TW_SHOT", "1,1");
  SetEnvironmentVariableA("RENODX_TW_PROFILE", "1");

  mock::Device game_device;
  game_device.native = 0x9000;
  mock::Device proxy_device;
  mock::Runtime game_runtime;
  game_runtime.owner = &game_device;
  mock::Runtime proxy_runtime;
  proxy_runtime.owner = &proxy_device;
  proxy::use_device_proxy = (mode == "proxy" || mode == "failed");
  proxy::device_proxy_creation_failed = (mode == "failed");
  proxy::proxy_device_reshade = (expect_proxy ? &proxy_device : nullptr);
  mock::Runtime* answering = (expect_proxy ? &proxy_runtime : &game_runtime);

  hooks::Install(&rows, true, "[Test]");
  CHECK(registered_present == reinterpret_cast<void*>(&hooks::internal::OnReshadePresent));

  // The game's runtime presents first behind the proxy: no clock, no change, no screenshot.
  if (expect_proxy) {
    hooks::internal::OnReshadePresent(&game_runtime);
    CHECK(hooks::internal::clock_start.load() == 0 && bloom == 1.f && game_runtime.screenshots == 0 && profiles == 0);
  }
  // The answering runtime's first present starts the clock: the 0 s changes now, the 1 s shot and profile not yet.
  hooks::internal::OnReshadePresent(answering);
  CHECK(hooks::internal::clock_start.load() != 0);
  CHECK(bloom == 0.5f && answering->screenshots == 1 && profiles == 0);
  CHECK(page_resets == 2 && ao_resets == 1);

  hooks::internal::clock_start.store(GetTickCount64() - 2000);
  if (expect_proxy) {
    hooks::internal::OnReshadePresent(&game_runtime);
    CHECK(game_runtime.screenshots == 0 && profiles == 0);
  }
  hooks::internal::OnReshadePresent(answering);
  CHECK(answering->screenshots == 2 && profiles == 1);
  hooks::internal::OnReshadePresent(answering);
  CHECK(answering->screenshots == 2 && profiles == 1);
  CHECK(game_runtime.screenshots + proxy_runtime.screenshots == 2);

  hooks::Uninstall();
  CHECK(registered_present == nullptr);
  std::printf("PASS %s: SET, SHOT and PROFILE answered by the %s runtime only\n", mode.c_str(), expect_proxy ? "proxy's" : "game's");
  return 0;
}
