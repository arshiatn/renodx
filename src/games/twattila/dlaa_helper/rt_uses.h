/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <d3d12.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <map>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The resident's resource-use log (rt_serve.h's `run` with log_uses; recorder builds only; gto_design M10; the format:
// tools/tw/round_notes/plan_1117/designs/grs_uses_log.md): what each request list's compute work binds, copies and builds,
// as the TSV rows tools/tw/lab/dead_outputs.py reads. Each list is seen through a copy of its vtable with these methods
// logged, its own vtable pointer swapped while the log is on (the list, its methods and what D3D12 gets stay the same).
namespace twattila::dlaa_helper::uses {

// The logged methods' vtable slots (d3d12.h's ID3D12GraphicsCommandList4 order: COM's binary layout).
enum Method : size_t {
  RESET = 10,
  DISPATCH = 14,
  COPY_BUFFER_REGION = 15,
  COPY_TEXTURE_REGION = 16,
  COPY_RESOURCE = 17,
  SET_PIPELINE_STATE = 25,
  SET_COMPUTE_ROOT_SIGNATURE = 29,
  SET_COMPUTE_ROOT_DESCRIPTOR_TABLE = 31,
  SET_COMPUTE_ROOT_SHADER_RESOURCE_VIEW = 39,
  SET_COMPUTE_ROOT_UNORDERED_ACCESS_VIEW = 41,
  EXECUTE_INDIRECT = 59,
  BUILD_RAYTRACING_ACCELERATION_STRUCTURE = 72,
  COPY_RAYTRACING_ACCELERATION_STRUCTURE = 74,
  DISPATCH_RAYS = 76,
};

// The log of `Scene`'s lists (RtScene12), which names what they use:
//   bool UseOwnRoot(ID3D12RootSignature*): its root signature (another's bindings are not logged);
//   std::string UseStem(ID3D12PipelineState*): the stem's name (its DXIL beside the log);
//   void UseTable(D3D12_GPU_DESCRIPTOR_HANDLE, emit(kind, slot, resource)): a table's views ("uav" / "srv", "u28:0");
//   std::string UseRootSlot(UINT): a root SRV's or UAV's register ("t0:0");
//   std::string UseResource(ID3D12Resource*), UseAddress(D3D12_GPU_VIRTUAL_ADDRESS): a resource's name;
//   bool UseReadBack(ID3D12Resource*): in a readback heap (the CPU reads it).
template <typename Scene>
class Log {
 public:
  explicit Log(Scene* scene) : scene_(scene) {}
  Log(const Log&) = delete;
  Log& operator=(const Log&) = delete;
  ~Log() {
    for (ID3D12GraphicsCommandList4* list : hooked_) {
      if (*reinterpret_cast<void***>(list) == table_.data()) *reinterpret_cast<void***>(list) = original_;
    }
    if (active_ == this) active_ = nullptr;
  }

  // `lists` (one kind's: one vtable) logged until the log is destroyed. False: `why` says why.
  bool Hook(std::span<ID3D12GraphicsCommandList4* const> lists, std::string* why) {
    if (active_ != nullptr || lists.empty()) {
      *why = (active_ != nullptr) ? "a resource-use log is on already" : "no lists";
      return false;
    }
    void** original = *reinterpret_cast<void***>(lists.front());
    // The vtable's length: the newest interface its object answers as itself (ID3D12GraphicsCommandList4's 77 methods
    // to ID3D12GraphicsCommandList10's 86).
    const std::array<std::pair<IID, size_t>, 6> newer = {{{__uuidof(ID3D12GraphicsCommandList10), 86},
                                                         {__uuidof(ID3D12GraphicsCommandList9), 84},
                                                         {__uuidof(ID3D12GraphicsCommandList8), 82},
                                                         {__uuidof(ID3D12GraphicsCommandList7), 81},
                                                         {__uuidof(ID3D12GraphicsCommandList6), 80},
                                                         {__uuidof(ID3D12GraphicsCommandList5), 79}}};
    const auto found = std::ranges::find_if(newer, [&](const std::pair<IID, size_t>& kind) {
      Microsoft::WRL::ComPtr<IUnknown> other;
      return SUCCEEDED(lists.front()->QueryInterface(kind.first, reinterpret_cast<void**>(other.GetAddressOf())))
             && static_cast<void*>(other.Get()) == static_cast<void*>(lists.front());
    });
    for (ID3D12GraphicsCommandList4* list : lists) {
      if (*reinterpret_cast<void***>(list) != original) {
        *why = "the lists have different vtables";
        return false;
      }
    }
    table_.assign(original, original + ((found != newer.end()) ? found->second : 77));
    table_[RESET] = reinterpret_cast<void*>(&Reset);
    table_[DISPATCH] = reinterpret_cast<void*>(&Dispatch);
    table_[COPY_BUFFER_REGION] = reinterpret_cast<void*>(&CopyBufferRegion);
    table_[COPY_TEXTURE_REGION] = reinterpret_cast<void*>(&CopyTextureRegion);
    table_[COPY_RESOURCE] = reinterpret_cast<void*>(&CopyResource);
    table_[SET_PIPELINE_STATE] = reinterpret_cast<void*>(&SetPipelineState);
    table_[SET_COMPUTE_ROOT_SIGNATURE] = reinterpret_cast<void*>(&SetComputeRootSignature);
    table_[SET_COMPUTE_ROOT_DESCRIPTOR_TABLE] = reinterpret_cast<void*>(&SetComputeRootDescriptorTable);
    table_[SET_COMPUTE_ROOT_SHADER_RESOURCE_VIEW] = reinterpret_cast<void*>(&SetComputeRootShaderResourceView);
    table_[SET_COMPUTE_ROOT_UNORDERED_ACCESS_VIEW] = reinterpret_cast<void*>(&SetComputeRootUnorderedAccessView);
    table_[EXECUTE_INDIRECT] = reinterpret_cast<void*>(&ExecuteIndirect);
    table_[BUILD_RAYTRACING_ACCELERATION_STRUCTURE] = reinterpret_cast<void*>(&BuildRaytracingAccelerationStructure);
    table_[COPY_RAYTRACING_ACCELERATION_STRUCTURE] = reinterpret_cast<void*>(&CopyRaytracingAccelerationStructure);
    table_[DISPATCH_RAYS] = reinterpret_cast<void*>(&DispatchRays);
    original_ = original;
    active_ = this;
    for (ID3D12GraphicsCommandList4* list : lists) {
      *reinterpret_cast<void***>(list) = table_.data();
      hooked_.push_back(list);
    }
    return true;
  }

  // The rows from here on: frame `frame`'s (its spans counted from 1).
  void Frame(uint32_t frame) {
    const std::lock_guard lock(mutex_);
    frame_ = frame;
    span_ = 0;
    seen_.clear();
  }
  // A Mark() pass: the list's next span.
  void Span(ID3D12GraphicsCommandList* list, std::wstring_view pass) {
    const std::lock_guard lock(mutex_);
    Begin(&lists_[list], std::string(pass.begin(), pass.end()));
  }
  // A resource read outside the logged work (shared with the game, dumped, read by another root signature's passes).
  void External(const std::string& resource) {
    const std::lock_guard lock(mutex_);
    externals_.insert(resource);
  }
  // The rows so far (then none); the externals' at the end.
  std::string TakeRows() {
    const std::lock_guard lock(mutex_);
    return std::exchange(rows_, {});
  }
  std::string ExternalRows() {
    const std::lock_guard lock(mutex_);
    std::string rows;
    for (const std::string& resource : externals_) rows += std::format("-\t-\t-\texternal\t-\t{}\n", resource);
    return rows;
  }

 private:
  // A list's state as D3D12 keeps it (Reset clears it, a new root signature its arguments) and its span.
  struct ListState {
    ID3D12PipelineState* pipeline = nullptr;
    ID3D12RootSignature* root = nullptr;
    std::map<UINT, D3D12_GPU_DESCRIPTOR_HANDLE> tables;
    std::map<UINT, std::pair<D3D12_GPU_VIRTUAL_ADDRESS, bool>> views;  // a root SRV's or UAV's (true) address
    uint32_t span = 0;
  };

  void Begin(ListState* state, std::string_view pass) {
    state->span = ++span_;
    rows_ += std::format("# {}\t{}\t{}\n", frame_, state->span, pass);
  }
  void Row(const ListState& state, std::string_view stem, std::string_view kind, std::string_view slot, const std::string& resource) {
    std::string row = std::format("{}\t{}\t{}\t{}\t{}\t{}\n", frame_, state.span, stem, kind, slot, resource);
    if (seen_.insert(row).second) rows_ += row;
  }
  void Wrote(const ListState& state, const std::string& resource, bool external) {
    Row(state, "-", "copy_dst", "-", resource);
    if (external) externals_.insert(resource);
  }
  // A dispatch's bindings (another root signature's: none): its tables' views, its root SRVs and UAVs. A root UAV's
  // buffer is external: NVAPI's cluster and partitioned TLAS operations read the geometry passes' outputs there (not logged).
  void Bound(const ListState& state, std::string_view stem) {
    if (!scene_->UseOwnRoot(state.root)) return;
    for (const auto& [parameter, table] : state.tables) {
      scene_->UseTable(table, [&](std::string_view kind, std::string_view slot, const std::string& resource) { Row(state, stem, kind, slot, resource); });
    }
    for (const auto& [parameter, view] : state.views) {
      const std::string resource = scene_->UseAddress(view.first);
      Row(state, stem, view.second ? "uav" : "srv", scene_->UseRootSlot(parameter), resource);
      if (view.second) externals_.insert(resource);
    }
  }

  template <Method M, typename F>
  static F Original(F) {
    return reinterpret_cast<F>(original_[M]);
  }
  static ListState& State(ID3D12GraphicsCommandList4* self) { return active_->lists_[self]; }

  static HRESULT STDMETHODCALLTYPE Reset(ID3D12GraphicsCommandList4* self, ID3D12CommandAllocator* allocator, ID3D12PipelineState* pipeline) {
    {
      const std::lock_guard lock(active_->mutex_);
      ListState& state = State(self);
      state = {.pipeline = pipeline};
      active_->Begin(&state, "(list start)");
    }
    return Original<RESET>(&Reset)(self, allocator, pipeline);
  }
  static void STDMETHODCALLTYPE Dispatch(ID3D12GraphicsCommandList4* self, UINT x, UINT y, UINT z) {
    {
      const std::lock_guard lock(active_->mutex_);
      const ListState& state = State(self);
      active_->Bound(state, active_->scene_->UseStem(state.pipeline));
    }
    Original<DISPATCH>(&Dispatch)(self, x, y, z);
  }
  static void STDMETHODCALLTYPE CopyBufferRegion(ID3D12GraphicsCommandList4* self, ID3D12Resource* destination, UINT64 destination_offset,
                                                 ID3D12Resource* source, UINT64 source_offset, UINT64 bytes) {
    {
      const std::lock_guard lock(active_->mutex_);
      const ListState& state = State(self);
      active_->Row(state, "-", "copy_src", "-", active_->scene_->UseResource(source));
      active_->Wrote(state, active_->scene_->UseResource(destination), active_->scene_->UseReadBack(destination));
    }
    Original<COPY_BUFFER_REGION>(&CopyBufferRegion)(self, destination, destination_offset, source, source_offset, bytes);
  }
  static void STDMETHODCALLTYPE CopyTextureRegion(ID3D12GraphicsCommandList4* self, const D3D12_TEXTURE_COPY_LOCATION* destination, UINT x, UINT y,
                                                  UINT z, const D3D12_TEXTURE_COPY_LOCATION* source, const D3D12_BOX* box) {
    {
      const std::lock_guard lock(active_->mutex_);
      const ListState& state = State(self);
      active_->Row(state, "-", "copy_src", "-", active_->scene_->UseResource(source->pResource));
      active_->Wrote(state, active_->scene_->UseResource(destination->pResource), active_->scene_->UseReadBack(destination->pResource));
    }
    Original<COPY_TEXTURE_REGION>(&CopyTextureRegion)(self, destination, x, y, z, source, box);
  }
  static void STDMETHODCALLTYPE CopyResource(ID3D12GraphicsCommandList4* self, ID3D12Resource* destination, ID3D12Resource* source) {
    {
      const std::lock_guard lock(active_->mutex_);
      const ListState& state = State(self);
      active_->Row(state, "-", "copy_src", "-", active_->scene_->UseResource(source));
      active_->Wrote(state, active_->scene_->UseResource(destination), active_->scene_->UseReadBack(destination));
    }
    Original<COPY_RESOURCE>(&CopyResource)(self, destination, source);
  }
  static void STDMETHODCALLTYPE SetPipelineState(ID3D12GraphicsCommandList4* self, ID3D12PipelineState* pipeline) {
    {
      const std::lock_guard lock(active_->mutex_);
      State(self).pipeline = pipeline;
    }
    Original<SET_PIPELINE_STATE>(&SetPipelineState)(self, pipeline);
  }
  static void STDMETHODCALLTYPE SetComputeRootSignature(ID3D12GraphicsCommandList4* self, ID3D12RootSignature* root) {
    {
      const std::lock_guard lock(active_->mutex_);
      ListState& state = State(self);
      if (state.root != root) {
        state.tables.clear();
        state.views.clear();
      }
      state.root = root;
    }
    Original<SET_COMPUTE_ROOT_SIGNATURE>(&SetComputeRootSignature)(self, root);
  }
  static void STDMETHODCALLTYPE SetComputeRootDescriptorTable(ID3D12GraphicsCommandList4* self, UINT parameter, D3D12_GPU_DESCRIPTOR_HANDLE table) {
    {
      const std::lock_guard lock(active_->mutex_);
      State(self).tables[parameter] = table;
    }
    Original<SET_COMPUTE_ROOT_DESCRIPTOR_TABLE>(&SetComputeRootDescriptorTable)(self, parameter, table);
  }
  static void STDMETHODCALLTYPE SetComputeRootShaderResourceView(ID3D12GraphicsCommandList4* self, UINT parameter, D3D12_GPU_VIRTUAL_ADDRESS address) {
    {
      const std::lock_guard lock(active_->mutex_);
      State(self).views[parameter] = {address, false};
    }
    Original<SET_COMPUTE_ROOT_SHADER_RESOURCE_VIEW>(&SetComputeRootShaderResourceView)(self, parameter, address);
  }
  static void STDMETHODCALLTYPE SetComputeRootUnorderedAccessView(ID3D12GraphicsCommandList4* self, UINT parameter, D3D12_GPU_VIRTUAL_ADDRESS address) {
    {
      const std::lock_guard lock(active_->mutex_);
      State(self).views[parameter] = {address, true};
    }
    Original<SET_COMPUTE_ROOT_UNORDERED_ACCESS_VIEW>(&SetComputeRootUnorderedAccessView)(self, parameter, address);
  }
  static void STDMETHODCALLTYPE ExecuteIndirect(ID3D12GraphicsCommandList4* self, ID3D12CommandSignature* signature, UINT most, ID3D12Resource* arguments,
                                                UINT64 arguments_offset, ID3D12Resource* count, UINT64 count_offset) {
    {
      const std::lock_guard lock(active_->mutex_);
      const ListState& state = State(self);
      active_->Bound(state, active_->scene_->UseStem(state.pipeline));
      active_->Row(state, "-", "copy_src", "-", active_->scene_->UseResource(arguments));
      if (count != nullptr) active_->Row(state, "-", "copy_src", "-", active_->scene_->UseResource(count));
    }
    Original<EXECUTE_INDIRECT>(&ExecuteIndirect)(self, signature, most, arguments, arguments_offset, count, count_offset);
  }
  // A build reads its source and a TLAS's instances, writes its destination (external: BLASes are read through the TLAS's
  // instances, a TLAS by ray queries) and its postbuild info. A BLAS's geometry is not logged (the root UAVs' buffers).
  static void STDMETHODCALLTYPE BuildRaytracingAccelerationStructure(ID3D12GraphicsCommandList4* self, const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC* desc,
                                                                     UINT infos, const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC* info) {
    {
      const std::lock_guard lock(active_->mutex_);
      const ListState& state = State(self);
      if (desc->Inputs.Type == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL && desc->Inputs.NumDescs != 0) {
        active_->Row(state, "-", "copy_src", "-", active_->scene_->UseAddress(desc->Inputs.InstanceDescs));
      }
      if (desc->SourceAccelerationStructureData != 0) {
        active_->Row(state, "-", "copy_src", "-", active_->scene_->UseAddress(desc->SourceAccelerationStructureData));
      }
      active_->Wrote(state, active_->scene_->UseAddress(desc->DestAccelerationStructureData), true);
      for (UINT index = 0; index < infos; ++index) active_->Wrote(state, active_->scene_->UseAddress(info[index].DestBuffer), false);
    }
    Original<BUILD_RAYTRACING_ACCELERATION_STRUCTURE>(&BuildRaytracingAccelerationStructure)(self, desc, infos, info);
  }
  static void STDMETHODCALLTYPE CopyRaytracingAccelerationStructure(ID3D12GraphicsCommandList4* self, D3D12_GPU_VIRTUAL_ADDRESS destination,
                                                                    D3D12_GPU_VIRTUAL_ADDRESS source, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE mode) {
    {
      const std::lock_guard lock(active_->mutex_);
      const ListState& state = State(self);
      active_->Row(state, "-", "copy_src", "-", active_->scene_->UseAddress(source));
      active_->Wrote(state, active_->scene_->UseAddress(destination), true);
    }
    Original<COPY_RAYTRACING_ACCELERATION_STRUCTURE>(&CopyRaytracingAccelerationStructure)(self, destination, source, mode);
  }
  // Path Tracing SER's library (a state object): out of v1, its stem has no DXIL beside the log (all it binds counts as
  // read and written).
  static void STDMETHODCALLTYPE DispatchRays(ID3D12GraphicsCommandList4* self, const D3D12_DISPATCH_RAYS_DESC* desc) {
    {
      const std::lock_guard lock(active_->mutex_);
      active_->Bound(State(self), "ser_library");
    }
    Original<DISPATCH_RAYS>(&DispatchRays)(self, desc);
  }

  static inline Log* active_ = nullptr;
  static inline void** original_ = nullptr;
  Scene* scene_;
  std::vector<void*> table_;
  std::vector<ID3D12GraphicsCommandList4*> hooked_;
  std::mutex mutex_;
  std::map<ID3D12GraphicsCommandList*, ListState> lists_;
  uint32_t frame_ = 0;
  uint32_t span_ = 0;
  std::set<std::string> seen_;  // the frame's rows (each once)
  std::string rows_;
  std::set<std::string> externals_;
};

}  // namespace twattila::dlaa_helper::uses
