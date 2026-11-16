// command_action's fast dispatch (src/utils/command_action_fast.hpp, the Total War add-ons' opt-in) against RunCallbacks,
// command by command, with ReShade's event lists and objects mocked (state, pipeline and command_action as an add-on
// runs them; real DXBC from D3DCompile, no GPU). Every command runs twice: RunCallbacks alone (the fast hook taken out for
// that call), then the fast dispatch, whose per-command-list cache lives on across commands. The callbacks each ran (in
// order, with stage, matched hash and result), the post / finalize callbacks, the replayed draw and the skip must match.
// 1. Draws, indexed draws, dispatches over vertex, pixel and compute pipelines; per-shader callbacks returning {}, post,
//    finalize, both, replay or bypass (decided by callback and command); one callback on two stages (once a command);
//    command-wide callbacks, one ungated, one gated (a frame flag and a pipeline set).
// 2. Handle reuse: a bound pipeline destroyed and another shader created at its handle, with and without a new bind.
// 3. Register after bind (per shader: not on pipelines made before, as RunCallbacks; command-wide: at once), Unregister
//    after bind.
// 4. Multi-stage fallback: a pipeline bound at two stages goes to RunCallbacks (the fast dispatch declines it).
// 5. Deferred context: a second command list with its own binds, interleaved, then destroyed (its cache freed).
// 6. Device loss: every pipeline and command list destroyed, a new device whose pipelines reuse the old handles.
// 7. Mixed modules: a module attached to command_action without the opt-in sends every command to RunCallbacks; once it
//    detaches, the fast dispatch takes the commands again with every slot resolved anew.
// 8. Two modules (partner.cpp, a DLL with its own command_action and opt-in): the partner registers a command-wide
//    callback, detaches, and only then unregisters it (as resource_upgrade does after command_action's detach) while
//    this module goes on drawing; that late Unregister must move the epoch.
// 9. A gate's module learns of a pipeline after command_action's init callback (view_share's order): stale until it
//    calls fast::Invalidate(), then the same callbacks again.
// 10. Gates full: every gate taken, one more SetGate is refused with a warning and its callback stays ungated (gated as
//     never active while it acts, it would show a difference were the gate kept).
// CONTROL: a gate that says "inactive" while its callback acts must show a difference.
// The fast dispatch must have taken the commands it should (counted), else the comparison proves nothing.
#include <windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <cstdio>
#include <format>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "src/utils/command_action_fast.hpp"
#include "src/utils/hash.hpp"
#include "test/tw_particle_history/mock_reshade.hpp"

namespace action = renodx::utils::command_action;
namespace fast = renodx::utils::command_action::fast;
using api_event = reshade::addon_event;
using namespace reshade::api;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

#if defined(_WIN64)
constexpr const wchar_t* PARTNER_DLL = L"fast_draws_partner_amd64.dll";
#else
constexpr const wchar_t* PARTNER_DLL = L"fast_draws_partner_x86.dll";
#endif

// ---- ReShade: the exports the utilities call, the event lists, private data, the log's warnings.
static std::array<std::vector<void*>, 512> event_lists;
static std::vector<std::string> warnings;
extern "C" __declspec(dllexport) const char* const NAME = "Draw dispatch test";
extern "C" {
__declspec(dllexport) bool ReShadeRegisterAddon(void*, uint32_t) { return true; }
__declspec(dllexport) void ReShadeUnregisterAddon(void*) {}
__declspec(dllexport) void ReShadeLogMessage(void*, int level, const char* message) {
  if (level == static_cast<int>(reshade::log::level::warning)) warnings.emplace_back(message);
}
__declspec(dllexport) bool ReShadeGetConfigValue(void*, void*, const char*, const char*, char*, size_t*) { return false; }
__declspec(dllexport) void ReShadeSetConfigValue(void*, void*, const char*, const char*, const char*) {}
__declspec(dllexport) void ReShadeRegisterEvent(api_event ev, void* callback) { event_lists[static_cast<size_t>(ev)].push_back(callback); }
__declspec(dllexport) void ReShadeRegisterEventForAddon(void*, api_event ev, void* callback) {
  event_lists[static_cast<size_t>(ev)].push_back(callback);
}
__declspec(dllexport) void ReShadeUnregisterEvent(api_event ev, void* callback) { std::erase(event_lists[static_cast<size_t>(ev)], callback); }
__declspec(dllexport) void ReShadeUnregisterEventForAddon(void*, api_event ev, void* callback) {
  std::erase(event_lists[static_cast<size_t>(ev)], callback);
}
}

template <api_event ev>
using Decl = typename reshade::addon_event_traits<ev>::decl;

template <api_event ev, typename... Args>
void InvokeVoid(Args... args) {
  const auto list = event_lists[static_cast<size_t>(ev)];
  for (void* callback : list) reinterpret_cast<Decl<ev>>(callback)(args...);
}

template <api_event ev, typename... Args>
bool InvokeBool(Args... args) {
  bool skip = false;
  const auto list = event_lists[static_cast<size_t>(ev)];
  for (void* callback : list) skip = reinterpret_cast<Decl<ev>>(callback)(args...) || skip;
  return skip;
}

struct PrivateData {
  std::unordered_map<std::string, uint64_t> values;
  void Get(const uint8_t guid[16], uint64_t* data) const {
    const auto found = values.find(std::string(reinterpret_cast<const char*>(guid), 16));
    *data = (found != values.end()) ? found->second : 0u;
  }
  void Set(const uint8_t guid[16], uint64_t data) {
    const std::string key(reinterpret_cast<const char*>(guid), 16);
    if (data != 0u) {
      values[key] = data;
    } else {
      values.erase(key);
    }
  }
};

static std::vector<std::string> log_lines;

struct TestDevice : mock::Device {
  PrivateData private_data;
  void get_private_data(const uint8_t guid[16], uint64_t* data) const override { private_data.Get(guid, data); }
  void set_private_data(const uint8_t guid[16], const uint64_t data) override { private_data.Set(guid, data); }
};

struct TestList : mock::CommandList {
  PrivateData private_data;
  void get_private_data(const uint8_t guid[16], uint64_t* data) const override { private_data.Get(guid, data); }
  void set_private_data(const uint8_t guid[16], const uint64_t data) override { private_data.Set(guid, data); }
  void draw(uint32_t, uint32_t, uint32_t, uint32_t) override { log_lines.emplace_back("REPLAY"); }
  void draw_indexed(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) override { log_lines.emplace_back("REPLAY"); }
  void dispatch(uint32_t, uint32_t, uint32_t) override { log_lines.emplace_back("REPLAY"); }
};

// ---- The callbacks: each one's result by its id and the command (both runs of a command see the same).
static uint64_t command_number = 0u;
static bool frame_scaled = false;                // the gated command-wide callback's frame flag
static std::set<uint64_t> patched_pipelines;     // ... and its pipelines (pixel shaders)
static std::array<int, 256> ids = {};

template <typename Context>
std::string Where(const Context& context) {
  const int stage = context.matched_shader_stage.has_value() ? static_cast<int>(*context.matched_shader_stage) : 0;
  const uint64_t pipeline = context.matched_stage_state.has_value() ? context.matched_stage_state->pipeline.handle : 0u;
  return std::format("stage {} hash {:08X} pipeline {:x} applied {}", stage, context.matched_shader_hash, pipeline,
                     context.matched_stage_state.has_value() ? static_cast<int>(context.matched_stage_state->applied_stage) : 0);
}

template <typename Context>
void Post(Context& context, const void* data) {
  log_lines.push_back(std::format("post {} {}", *static_cast<const int*>(data), Where(context)));
}

template <typename Context>
void Finalize(Context& context, const void* data) {
  log_lines.push_back(std::format("finalize {} {}", *static_cast<const int*>(data), Where(context)));
}

template <int ID, typename Context>
action::CallbackResult<Context> Scripted(Context& context) {
  log_lines.push_back(std::format("{} {} command {}", ID, Where(context), context.command_id != 0u));
  const uint64_t roll = (command_number * 2654435761u + ID * 40503u) % 100u;
  if (roll < 55u) return {};
  if (roll < 65u) return {.post_callback = &Post<Context>, .post_data = &ids[ID]};
  if (roll < 73u) return {.post_data = &ids[ID], .finalize_callback = &Finalize<Context>};
  if (roll < 80u) return {.post_callback = &Post<Context>, .post_data = &ids[ID], .finalize_callback = &Finalize<Context>};
  if (roll < 90u) return {.replay = true};
  return {.post_data = &ids[ID], .bypass = true, .finalize_callback = &Finalize<Context>};
}

template <int ID>
constexpr auto SCRIPTED_CALLBACK = []<typename Context>(Context& context) -> action::CallbackResult<Context> { return Scripted<ID>(context); };

// Command-wide, ungated: always runs.
constexpr auto ANY_COMMAND = []<typename Context>(Context& context) -> action::CallbackResult<Context> {
  log_lines.push_back(std::format("any {}", context.matched_shader_hash));
  return {};
};

// Command-wide, gated: acts only while frame_scaled and the bound pixel (compute) shader is patched; else {} untouched.
constexpr auto SCALED_COMMAND = []<typename Context>(Context& context) -> action::CallbackResult<Context> {
  if (!frame_scaled) return {};
  constexpr bool COMPUTE = std::is_same_v<typename Context::ArgumentType, action::DispatchArguments>;
  const auto pipeline = renodx::utils::state::GetBoundShaderPipeline(
      context.cmd_list, COMPUTE ? pipeline_stage::compute_shader : pipeline_stage::pixel_shader, &context.bound_state_cache);
  if (!patched_pipelines.contains(pipeline.handle)) return {};
  log_lines.push_back(std::format("scaled {:x}", pipeline.handle));
  return {.post_callback = &Post<Context>, .post_data = &ids[200]};
};

// CONTROL: gated as never active, yet it acts.
constexpr auto LYING_COMMAND = []<typename Context>(Context&) -> action::CallbackResult<Context> {
  log_lines.emplace_back("lying");
  return {};
};

// Case 10: the gates' fillers (a gate each: one type each) and the callback past the last gate, which acts while its
// gate says never.
template <size_t N>
constexpr auto FILLER_COMMAND = []<typename Context>(Context&) -> action::CallbackResult<Context> { return {}; };
constexpr auto OVERFLOW_COMMAND = []<typename Context>(Context&) -> action::CallbackResult<Context> {
  log_lines.emplace_back("overflow");
  return {};
};

// ---- Shaders: real DXBC, one per constant.
static std::vector<uint8_t> Compile(const char* target, int constant) {
  std::string source;
  if (target[0] == 'v') source = std::format("float4 main(float4 p : POSITION) : SV_Position {{ return p * {}.0; }}", constant);
  if (target[0] == 'p') source = std::format("float4 main() : SV_Target {{ return {}.0; }}", constant);
  if (target[0] == 'c') source = std::format("RWBuffer<float> o : register(u0); [numthreads(1, 1, 1)] void main() {{ o[0] = {}.0; }}", constant);
  Microsoft::WRL::ComPtr<ID3DBlob> blob;
  Microsoft::WRL::ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, "main", target, 0, 0, &blob, &errors))) return {};
  const auto* bytes = static_cast<const uint8_t*>(blob->GetBufferPointer());
  return {bytes, bytes + blob->GetBufferSize()};
}

struct Shader {
  std::vector<uint8_t> code;
  uint32_t hash = 0u;
  pipeline_subobject_type type = pipeline_subobject_type::vertex_shader;
};

static Shader MakeShader(const char* target, int constant) {
  Shader shader = {.code = Compile(target, constant)};
  shader.hash = renodx::utils::hash::ComputeCRC32(shader.code.data(), shader.code.size());
  shader.type = (target[0] == 'v') ? pipeline_subobject_type::vertex_shader
                                   : ((target[0] == 'p') ? pipeline_subobject_type::pixel_shader : pipeline_subobject_type::compute_shader);
  return shader;
}

static void CreatePipeline(device* owner, uint64_t handle, std::initializer_list<const Shader*> shaders) {
  std::vector<shader_desc> descs;
  descs.reserve(shaders.size());
  std::vector<pipeline_subobject> subobjects;
  for (const Shader* shader : shaders) {
    descs.push_back({.code = shader->code.data(), .code_size = shader->code.size()});
    subobjects.push_back({shader->type, 1u, &descs.back()});
  }
  InvokeBool<api_event::create_pipeline>(owner, pipeline_layout{0u}, static_cast<uint32_t>(subobjects.size()), subobjects.data());
  InvokeVoid<api_event::init_pipeline>(owner, pipeline_layout{0u}, static_cast<uint32_t>(subobjects.size()), subobjects.data(),
                                       pipeline{handle});
}

static void DestroyPipeline(device* owner, uint64_t handle) { InvokeVoid<api_event::destroy_pipeline>(owner, pipeline{handle}); }

static void Bind(command_list* cmd_list, pipeline_stage stages, uint64_t handle) {
  InvokeVoid<api_event::bind_pipeline>(cmd_list, stages, pipeline{handle});
}

// ---- The fast dispatch's hook, counted.
static size_t fast_taken = 0u;
static size_t fast_declined = 0u;
template <typename Arguments>
bool CountedRun(action::CommandContext<Arguments>* context, bool* skip) {
  const bool taken = fast::internal::Run<Arguments>(context, skip);
  ++(taken ? fast_taken : fast_declined);
  return taken;
}

static void Hooks(bool on) {
  action::internal::fast_run_callbacks<action::DrawArguments> = on ? &CountedRun<action::DrawArguments> : nullptr;
  action::internal::fast_run_callbacks<action::DrawIndexedArguments> = on ? &CountedRun<action::DrawIndexedArguments> : nullptr;
  action::internal::fast_run_callbacks<action::DispatchArguments> = on ? &CountedRun<action::DispatchArguments> : nullptr;
}

enum class Kind { DRAW, INDEXED, DISPATCH };

static std::string RunCommand(command_list* cmd_list, Kind kind, bool fast_path) {
  log_lines.clear();
  Hooks(fast_path);
  bool skip = false;
  switch (kind) {
    case Kind::DRAW:     skip = InvokeBool<api_event::draw>(cmd_list, 3u, 1u, 0u, 0u); break;
    case Kind::INDEXED:  skip = InvokeBool<api_event::draw_indexed>(cmd_list, 6u, 1u, 0u, 0, 0u); break;
    case Kind::DISPATCH: skip = InvokeBool<api_event::dispatch>(cmd_list, 1u, 1u, 1u); break;
  }
  Hooks(true);
  std::string text;
  for (const auto& line : log_lines) text += line + " | ";
  return text + (skip ? "SKIP" : "draw");
}

static size_t commands = 0u;
static size_t differences = 0u;
static size_t callbacks_seen = 0u;

// One command both ways: RunCallbacks, then the fast dispatch.
static void Command(command_list* cmd_list, Kind kind) {
  ++command_number;
  const std::string legacy = RunCommand(cmd_list, kind, false);
  const size_t taken_before = fast_taken;
  const std::string fast_result = RunCommand(cmd_list, kind, true);
  ++commands;
  callbacks_seen += std::count(legacy.begin(), legacy.end(), '|');
  if (legacy != fast_result) {
    ++differences;
    if (differences <= 5u) {
      std::printf("  difference at command %llu (fast %s):\n    legacy %s\n    fast   %s\n", static_cast<unsigned long long>(command_number),
                  fast_taken > taken_before ? "taken" : "declined", legacy.c_str(), fast_result.c_str());
    }
  }
}

int main() {
  for (size_t index = 0u; index < ids.size(); ++index) ids[index] = static_cast<int>(index);

  const Shader vs1 = MakeShader("vs_5_0", 1);
  const Shader vs2 = MakeShader("vs_5_0", 2);
  const Shader vs3 = MakeShader("vs_5_0", 3);
  const Shader ps1 = MakeShader("ps_5_0", 1);
  const Shader ps2 = MakeShader("ps_5_0", 2);
  const Shader ps3 = MakeShader("ps_5_0", 3);
  const Shader cs1 = MakeShader("cs_5_0", 1);
  const Shader cs2 = MakeShader("cs_5_0", 2);
  CHECK(!vs1.code.empty() && !ps3.code.empty() && !cs2.code.empty());

  // Callbacks: a few per shader, one on two stages, two of them on compute; command-wide ones.
  action::Register(SCRIPTED_CALLBACK<1>, {.shader_hash = vs1.hash, .command_types = action::COMMAND_TYPE_DIRECT_DRAW});
  action::Register(SCRIPTED_CALLBACK<2>, {.shader_hash = vs1.hash, .command_types = action::COMMAND_TYPE_DRAW_INDEXED});
  action::Register(SCRIPTED_CALLBACK<3>, {.shader_hash = ps1.hash});
  action::Register(SCRIPTED_CALLBACK<4>, {.shader_hash = ps1.hash, .command_types = action::COMMAND_TYPE_DIRECT_DRAW});
  action::Register(SCRIPTED_CALLBACK<5>, {.shader_hash = vs2.hash});
  action::Register(SCRIPTED_CALLBACK<5>, {.shader_hash = ps2.hash});  // the same callback on two stages: once a command
  action::Register(SCRIPTED_CALLBACK<6>, {.shader_hash = ps2.hash, .command_types = action::COMMAND_TYPE_DRAW});
  action::Register(SCRIPTED_CALLBACK<7>, {.shader_hash = cs1.hash});
  action::Register(SCRIPTED_CALLBACK<8>, {.shader_hash = cs1.hash, .command_types = action::COMMAND_TYPE_DISPATCH});
  action::Register(SCRIPTED_CALLBACK<9>, {.shader_hash = vs3.hash});
  action::Register(ANY_COMMAND, {.command_types = action::COMMAND_TYPE_DRAW});
  action::Register(SCALED_COMMAND, {.command_types = action::COMMAND_TYPE_DIRECT_DRAW | action::COMMAND_TYPE_DISPATCH});
  fast::SetGate(SCALED_COMMAND, {
                                    .active = [](command_list*) { return frame_scaled; },
                                    .pipeline_active = [](pipeline pipeline, pipeline_stage stage) {
                                      return (stage == pipeline_stage::pixel_shader || stage == pipeline_stage::compute_shader)
                                             && patched_pipelines.contains(pipeline.handle);
                                    },
                                });
  // As an add-on attaches: its callbacks registered first (command_action registers the command events it has callbacks
  // for), then the opt-in.
  renodx::utils::command_action::Use(DLL_PROCESS_ATTACH);
  fast::Use(DLL_PROCESS_ATTACH);
  Hooks(true);

  auto device = std::make_unique<TestDevice>();
  auto immediate = std::make_unique<TestList>();
  immediate->owner = device.get();
  InvokeVoid<api_event::init_device>(static_cast<reshade::api::device*>(device.get()));
  InvokeVoid<api_event::init_command_list>(static_cast<command_list*>(immediate.get()));

  constexpr uint64_t VS_A = 0x1000, VS_B = 0x1040, VS_C = 0x1080, PS_A = 0x2000, PS_B = 0x2040, PS_C = 0x2080, CS_A = 0x3000, CS_B = 0x3040;
  CreatePipeline(device.get(), VS_A, {&vs1});
  CreatePipeline(device.get(), VS_B, {&vs2});
  CreatePipeline(device.get(), VS_C, {&vs3});
  CreatePipeline(device.get(), PS_A, {&ps1});
  CreatePipeline(device.get(), PS_B, {&ps2});
  CreatePipeline(device.get(), PS_C, {&ps3});
  CreatePipeline(device.get(), CS_A, {&cs1});
  CreatePipeline(device.get(), CS_B, {&cs2});
  patched_pipelines = {PS_B, CS_A};

  // 1. Mixed binds and commands, scaled and not.
  const std::array vertex = {VS_A, VS_B, VS_C, uint64_t{0}};
  const std::array pixel = {PS_A, PS_B, PS_C, uint64_t{0}};
  const std::array compute = {CS_A, CS_B, uint64_t{0}};
  auto* list = static_cast<command_list*>(immediate.get());
  for (int round = 0; round < 400; ++round) {
    frame_scaled = (round / 50) % 2 == 1;
    if (round % 3 == 0) Bind(list, pipeline_stage::vertex_shader, vertex[(round / 3) % vertex.size()]);
    if (round % 5 == 0) Bind(list, pipeline_stage::pixel_shader, pixel[(round / 5) % pixel.size()]);
    if (round % 7 == 0) Bind(list, pipeline_stage::compute_shader, compute[(round / 7) % compute.size()]);
    Command(list, static_cast<Kind>(round % 3));
  }
  const size_t taken_basic = fast_taken;
  std::printf("1. basic: %zu commands, %zu callback lines, fast taken %zu, declined %zu, differences %zu\n", commands, callbacks_seen,
              fast_taken, fast_declined, differences);
  CHECK(differences == 0u && fast_declined == 0u && fast_taken == commands);

  // 2. Handle reuse: VS_A destroyed, vs2's shader created at its handle; drawn without a new bind (the tracked slot still
  //    holds the handle), then bound again; then back to vs1.
  Bind(list, pipeline_stage::vertex_shader, VS_A);
  Bind(list, pipeline_stage::pixel_shader, PS_A);
  Command(list, Kind::INDEXED);
  DestroyPipeline(device.get(), VS_A);
  CreatePipeline(device.get(), VS_A, {&vs2});
  for (int i = 0; i < 6; ++i) Command(list, static_cast<Kind>(i % 2));
  Bind(list, pipeline_stage::vertex_shader, VS_A);
  for (int i = 0; i < 6; ++i) Command(list, static_cast<Kind>(i % 2));
  DestroyPipeline(device.get(), VS_A);
  CreatePipeline(device.get(), VS_A, {&vs1});
  Bind(list, pipeline_stage::vertex_shader, VS_A);
  for (int i = 0; i < 6; ++i) Command(list, static_cast<Kind>(i % 2));
  std::printf("2. handle reuse: differences %zu\n", differences);
  CHECK(differences == 0u);

  // 3. Register after bind: a per-shader callback on ps1 (not on PS_A, made before; on a pipeline made after), a
  //    command-wide one (at once); Unregister after bind.
  action::Register(SCRIPTED_CALLBACK<10>, {.shader_hash = ps1.hash});
  for (int i = 0; i < 6; ++i) Command(list, static_cast<Kind>(i % 2));
  constexpr uint64_t PS_D = 0x2100;
  CreatePipeline(device.get(), PS_D, {&ps1});
  Bind(list, pipeline_stage::pixel_shader, PS_D);
  for (int i = 0; i < 6; ++i) Command(list, static_cast<Kind>(i % 2));
  action::Register(SCRIPTED_CALLBACK<11>, {.command_types = action::COMMAND_TYPE_DRAW_INDEXED});
  for (int i = 0; i < 6; ++i) Command(list, static_cast<Kind>(i % 2));
  action::Unregister(SCRIPTED_CALLBACK<3>);
  action::Unregister(SCRIPTED_CALLBACK<11>);
  for (int i = 0; i < 6; ++i) Command(list, static_cast<Kind>(i % 2));
  std::printf("3. register / unregister after bind: differences %zu\n", differences);
  CHECK(differences == 0u);

  // 4. Multi-stage: one pipeline of vs3 + ps3 bound at both stages goes to RunCallbacks.
  constexpr uint64_t BOTH = 0x4000;
  action::Register(SCRIPTED_CALLBACK<12>, {.shader_hash = ps3.hash});
  CreatePipeline(device.get(), BOTH, {&vs3, &ps3});
  Bind(list, pipeline_stage::vertex_shader | pipeline_stage::pixel_shader, BOTH);
  const size_t declined_before = fast_declined;
  for (int i = 0; i < 6; ++i) Command(list, static_cast<Kind>(i % 2));
  std::printf("4. multi-stage: declined %zu of 6, differences %zu\n", fast_declined - declined_before, differences);
  CHECK(differences == 0u && fast_declined - declined_before == 6u);
  Bind(list, pipeline_stage::vertex_shader, VS_B);
  Bind(list, pipeline_stage::pixel_shader, PS_B);
  const size_t taken_after_multi = fast_taken;
  Command(list, Kind::INDEXED);
  CHECK(fast_taken == taken_after_multi + 1u);

  // 5. Deferred context, interleaved, then destroyed.
  auto deferred = std::make_unique<TestList>();
  deferred->owner = device.get();
  auto* second = static_cast<command_list*>(deferred.get());
  InvokeVoid<api_event::init_command_list>(second);
  Bind(second, pipeline_stage::vertex_shader, VS_C);
  Bind(second, pipeline_stage::pixel_shader, PS_B);
  Bind(second, pipeline_stage::compute_shader, CS_A);
  frame_scaled = true;
  for (int i = 0; i < 30; ++i) {
    if (i % 4 == 0) Bind(second, pipeline_stage::pixel_shader, pixel[i % pixel.size()]);
    if (i % 6 == 0) Bind(list, pipeline_stage::vertex_shader, vertex[i % vertex.size()]);
    Command((i % 2 == 0) ? second : list, static_cast<Kind>(i % 3));
  }
  const size_t caches_with_deferred = fast::internal::caches.size();
  InvokeVoid<api_event::destroy_command_list>(second);
  deferred.reset();
  std::printf("5. deferred context: caches %zu -> %zu, differences %zu\n", caches_with_deferred, fast::internal::caches.size(), differences);
  CHECK(differences == 0u && caches_with_deferred == 2u && fast::internal::caches.size() == 1u);

  // 6. Device loss: everything destroyed; a new device and immediate context whose pipelines reuse the handles with
  //    other shaders.
  for (const uint64_t handle : {VS_A, VS_B, VS_C, PS_A, PS_B, PS_C, PS_D, CS_A, CS_B, BOTH}) DestroyPipeline(device.get(), handle);
  InvokeVoid<api_event::destroy_command_list>(list);
  InvokeVoid<api_event::destroy_device>(static_cast<reshade::api::device*>(device.get()));
  immediate.reset();
  device.reset();
  CHECK(fast::internal::caches.empty());
  auto new_device = std::make_unique<TestDevice>();
  auto new_immediate = std::make_unique<TestList>();
  new_immediate->owner = new_device.get();
  InvokeVoid<api_event::init_device>(static_cast<reshade::api::device*>(new_device.get()));
  list = static_cast<command_list*>(new_immediate.get());
  InvokeVoid<api_event::init_command_list>(list);
  CreatePipeline(new_device.get(), VS_A, {&vs2});
  CreatePipeline(new_device.get(), VS_B, {&vs1});
  CreatePipeline(new_device.get(), PS_A, {&ps2});
  CreatePipeline(new_device.get(), PS_B, {&ps1});
  CreatePipeline(new_device.get(), CS_A, {&cs2});
  CreatePipeline(new_device.get(), CS_B, {&cs1});
  patched_pipelines = {PS_A, CS_B};
  for (int round = 0; round < 60; ++round) {
    if (round % 3 == 0) Bind(list, pipeline_stage::vertex_shader, (round % 2 == 0) ? VS_A : VS_B);
    if (round % 4 == 0) Bind(list, pipeline_stage::pixel_shader, (round % 8 == 0) ? PS_A : PS_B);
    if (round % 5 == 0) Bind(list, pipeline_stage::compute_shader, (round % 10 == 0) ? CS_A : CS_B);
    Command(list, static_cast<Kind>(round % 3));
  }
  std::printf("6. device loss: differences %zu\n", differences);
  CHECK(differences == 0u);

  // 7. Mixed modules: another module attached to command_action without the opt-in.
  auto* control = const_cast<renodx::utils::cross_addon::internal::ControlBlock<action::internal::SharedData>*>(fast::internal::action_control);
  CHECK(control != nullptr);
  static const uint8_t OTHER_MODULE = 0u;
  auto* other = renodx::utils::cross_addon::internal::CreateModule(*control, &OTHER_MODULE);
  CHECK(other != nullptr);
  other->active = true;
  const size_t declined_mixed = fast_declined;
  for (int i = 0; i < 9; ++i) Command(list, static_cast<Kind>(i % 3));
  const size_t mixed_declined = fast_declined - declined_mixed;
  other->active = false;
  const size_t taken_unmixed = fast_taken;
  for (int i = 0; i < 9; ++i) Command(list, static_cast<Kind>(i % 3));
  std::printf("7. mixed modules: declined %zu of 9 while attached, taken %zu of 9 after, differences %zu\n", mixed_declined,
              fast_taken - taken_unmixed, differences);
  CHECK(differences == 0u && mixed_declined == 9u && fast_taken - taken_unmixed == 9u);

  // 8. Two modules: the partner DLL next to this exe.
  std::wstring partner_path(MAX_PATH, L'\0');
  partner_path.resize(GetModuleFileNameW(nullptr, partner_path.data(), MAX_PATH));
  partner_path = partner_path.substr(0, partner_path.find_last_of(L"\\/") + 1) + PARTNER_DLL;
  HMODULE partner = LoadLibraryW(partner_path.c_str());
  CHECK(partner != nullptr);
  const auto partner_attach = reinterpret_cast<void (*)(void (*)(const char*))>(GetProcAddress(partner, "PartnerAttach"));
  const auto partner_detach = reinterpret_cast<void (*)()>(GetProcAddress(partner, "PartnerDetach"));
  const auto partner_unregister = reinterpret_cast<void (*)()>(GetProcAddress(partner, "PartnerUnregister"));
  CHECK(partner_attach != nullptr && partner_detach != nullptr && partner_unregister != nullptr);
  partner_attach([](const char* line) { log_lines.emplace_back(line); });
  const size_t taken_partner = fast_taken;
  for (int i = 0; i < 6; ++i) Command(list, static_cast<Kind>(i % 3));
  partner_detach();
  for (int i = 0; i < 6; ++i) Command(list, static_cast<Kind>(i % 3));
  const uint64_t epoch_before_unregister = fast::internal::shared.data->epoch.load();
  partner_unregister();
  const bool late_unregister_moved_epoch = fast::internal::shared.data->epoch.load() != epoch_before_unregister;
  for (int i = 0; i < 6; ++i) Command(list, static_cast<Kind>(i % 3));
  std::printf("8. two modules: taken %zu of 18, the late Unregister moved the epoch: %s, differences %zu\n",
              fast_taken - taken_partner, late_unregister_moved_epoch ? "yes" : "no", differences);
  CHECK(differences == 0u && late_unregister_moved_epoch && fast_taken - taken_partner == 18u);

  // 9. A gate's module learns of a pipeline after command_action's init callback moved the epoch (view_share fills its
  //    set in its own init callback): a command in between samples the old answer, which the cache keeps (one
  //    difference, expected here and not counted) until the module calls fast::Invalidate(). The order two threads can
  //    give, played on one.
  constexpr uint64_t PS_E = 0x2200;
  frame_scaled = true;
  CreatePipeline(new_device.get(), PS_E, {&ps3});
  Bind(list, pipeline_stage::pixel_shader, PS_E);
  Command(list, Kind::INDEXED);
  patched_pipelines.insert(PS_E);
  const size_t differences_before_late = differences;
  Command(list, Kind::INDEXED);
  const size_t stale_differences = differences - differences_before_late;
  differences = differences_before_late;
  fast::Invalidate();
  for (int i = 0; i < 6; ++i) Command(list, static_cast<Kind>(i % 2));
  std::printf("9. late gate answer: %zu difference before Invalidate (expected), differences after %zu\n", stale_differences,
              differences);
  CHECK(stale_differences == 1u && differences == 0u);

  // 10. Gates full: fillers take every gate left (SCALED_COMMAND holds one), then OVERFLOW_COMMAND's is refused.
  const size_t free_gates = fast::internal::MAX_GATES - fast::internal::shared.data->gate_count;
  const auto fillers = std::make_index_sequence<fast::internal::MAX_GATES>{};
  [&]<size_t... N>(std::index_sequence<N...>) {
    (..., [&] {
      if (N < free_gates) {
        fast::SetGate(FILLER_COMMAND<N>, {.active = [](command_list*) { return false; }});
      }
    }());
  }(fillers);
  const size_t warnings_before = warnings.size();
  CHECK(fast::internal::shared.data->gate_count == fast::internal::MAX_GATES);
  action::Register(OVERFLOW_COMMAND, {.command_types = action::COMMAND_TYPE_DRAW_INDEXED});
  fast::SetGate(OVERFLOW_COMMAND, {.active = [](command_list*) { return false; }});
  for (int i = 0; i < 6; ++i) Command(list, static_cast<Kind>(i % 2));
  std::printf("10. gates full: %zu of %zu taken, %zu warning (\"%s\"), differences %zu\n", fast::internal::shared.data->gate_count,
              fast::internal::MAX_GATES, warnings.size() - warnings_before, warnings.empty() ? "" : warnings.back().c_str(), differences);
  CHECK(warnings.size() == warnings_before + 1u && differences == 0u);
  [&]<size_t... N>(std::index_sequence<N...>) { (fast::ClearGate(FILLER_COMMAND<N>), ...); }(fillers);
  action::Unregister(OVERFLOW_COMMAND);
  CHECK(fast::internal::shared.data->gate_count == fast::internal::MAX_GATES - free_gates);

  std::printf("all:%zu commands, %zu callback lines, fast taken %zu (basic %zu), declined %zu, differences %zu\n", commands, callbacks_seen,
              fast_taken, taken_basic, fast_declined, differences);

  // CONTROL: a gate that lies.
  action::Register(LYING_COMMAND, {.command_types = action::COMMAND_TYPE_DRAW_INDEXED});
  fast::SetGate(LYING_COMMAND, {.active = [](command_list*) { return false; }});
  const size_t differences_before = differences;
  Command(list, Kind::INDEXED);
  std::printf("CONTROL lying gate: differences %zu (must be 1)\n", differences - differences_before);
  CHECK(differences - differences_before == 1u);
  fast::ClearGate(LYING_COMMAND);
  action::Unregister(LYING_COMMAND);
  fast::ClearGate(SCALED_COMMAND);
  fast::Use(DLL_PROCESS_DETACH);
  std::printf("PASS\n");
  return 0;
}
