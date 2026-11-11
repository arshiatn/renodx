#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <stdexcept>
#include <cstdio>
#include "src/games/twattila/texture_lod_bias.hpp"
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}
#include "test/tw_sampler_lifetime/mock_reshade.hpp"

void Check(bool good, const char* what) { if (!good) { printf("FAIL: %s\n",what); fflush(stdout); ExitProcess(1); } }
int main() {
  namespace lod=twattila::texture_lod_bias;
  mock::Device first,second;
  mock::CommandList commands,other;
  commands.owner=&first; other.owner=&second;
  Check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&first.native,nullptr,&commands.native)),"first device");
  Check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&second.native,nullptr,&other.native)),"second device");
  mock::Queue queue,other_queue;queue.commands=&commands;other_queue.commands=&other;
  reshade::api::sampler_desc desc={};desc.filter=reshade::api::filter_mode::anisotropic;
  reshade::api::sampler original={},other_original={};
  Check(first.create_sampler(desc,&original),"original");Check(second.create_sampler(desc,&other_original),"other original");
  auto* native_original=reinterpret_cast<ID3D11SamplerState*>(original.handle);
  auto* native_other=reinterpret_cast<ID3D11SamplerState*>(other_original.handle);
  commands.native->PSSetSamplers(0,1,&native_original);other.native->PSSetSamplers(0,1,&native_other);
  lod::SetMainView([] {return true;});lod::SetBias(-1.f);
  lod::SetTracking(&queue,false);
  Check(lod::internal::device_slots.size()==0 && lod::internal::tracking_devices.load()==0,"an untracked present makes no slots");
  lod::SetTracking(&queue,true);lod::SetTracking(&other_queue,true);
  Check(lod::internal::tracking_devices.load()==2,"two tracking devices counted");
  lod::OnBindViewports(&commands,0,0,nullptr);lod::OnBindViewports(&other,0,0,nullptr);
  uint64_t clone=0;
  lod::internal::samplers.if_contains(original.handle,[&](const auto& entry){clone=entry.second.clone.handle;});
  Check(clone!=0 && clone!=original.handle,"biased clone created");
  lod::OnInitSampler(&first,desc,original);
  lod::internal::samplers.if_contains(original.handle,[&](const auto& entry){Check(entry.second.clone.handle==clone,"duplicate init preserves clone");});
  first.destroy_sampler(original);second.destroy_sampler(other_original);
  Check(lod::internal::SlotsFor(&first)->game[0].Get()==native_original,"first slot owns original after game release");
  Check(lod::internal::SlotsFor(&second)->game[0].Get()==native_other,"second device has independent slots");
  D3D11_SAMPLER_DESC restored={};native_original->GetDesc(&restored);Check(restored.MipLODBias==0,"original remains alive while clone bound");
  // The per-bind path (the game's PSSetSamplers, then ReShade's push_descriptors): two more samplers of the game's.
  reshade::api::sampler pushed={},replacing={};
  desc.mip_lod_bias=0.5f;Check(first.create_sampler(desc,&pushed),"pushed sampler");
  desc.mip_lod_bias=0.25f;Check(first.create_sampler(desc,&replacing),"replacing sampler");
  desc.mip_lod_bias=0.f;
  auto* native_pushed=reinterpret_cast<ID3D11SamplerState*>(pushed.handle);
  auto* native_replacing=reinterpret_cast<ID3D11SamplerState*>(replacing.handle);
  const auto references=[](ID3D11SamplerState* sampler){sampler->AddRef();return sampler->Release();};
  const auto push=[&](uint32_t binding,uint32_t count,const reshade::api::sampler* samplers){
    std::array<ID3D11SamplerState*,lod::internal::SLOTS> natives={};
    const bool valid=binding<lod::internal::SLOTS && count<=lod::internal::SLOTS-binding;
    for(uint32_t index=0;valid && index<count;++index) natives[index]=reinterpret_cast<ID3D11SamplerState*>(samplers[index].handle);
    if(valid) commands.native->PSSetSamplers(binding,count,natives.data());
    lod::OnPushDescriptors(&commands,reshade::api::shader_stage::pixel,{0},0,{.binding=binding,.count=count,.type=reshade::api::descriptor_type::sampler,.descriptors=samplers});
  };
  const auto bound_at=[&](uint32_t slot){
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;commands.native->PSGetSamplers(slot,1,&sampler);
    return sampler;
  };
  const auto slots=lod::internal::SlotsFor(&first);
  Check(references(native_pushed)==1 && references(native_replacing)==1,"only the game's references before a bind");
  // A partial range: slots 2 and 3, the others as they were.
  const std::array<reshade::api::sampler,2> pair={pushed,replacing};
  push(2,2,pair.data());
  Check(slots->game[2].Get()==native_pushed && slots->game[3].Get()==native_replacing,"partial range recorded");
  Check(slots->game[0].Get()==native_original && !slots->game[1] && !slots->game[4],"partial range leaves the other slots");
  Check(references(native_pushed)==2 && references(native_replacing)==2,"a recorded sampler holds one reference");
  uint64_t pushed_clone=0;
  lod::internal::samplers.if_contains(pushed.handle,[&](const auto& entry){pushed_clone=entry.second.clone.handle;});
  Check(pushed_clone!=0 && bound_at(2).Get()==reinterpret_cast<ID3D11SamplerState*>(pushed_clone),"the bind is replaced by its clone in the main view");
  D3D11_SAMPLER_DESC clone_desc={};bound_at(2)->GetDesc(&clone_desc);Check(clone_desc.MipLODBias==-0.5f,"the clone carries the game's bias plus the addon's");
  // The same sampler again (the usual bind): no reference taken or dropped, the clone bound again over the game's.
  push(2,2,pair.data());
  Check(references(native_pushed)==2 && references(native_replacing)==2,"a repeated bind keeps one reference");
  Check(bound_at(2).Get()==reinterpret_cast<ID3D11SamplerState*>(pushed_clone),"a repeated bind is the clone again");
  // Another sampler in the slot: the first one's reference released exactly once.
  push(2,1,&replacing);
  Check(slots->game[2].Get()==native_replacing && references(native_pushed)==1 && references(native_replacing)==3,"a replaced sampler is released once");
  // The last slot, and ranges past it refused without a write.
  push(lod::internal::SLOTS-1,1,&pushed);
  Check(slots->game[lod::internal::SLOTS-1].Get()==native_pushed && references(native_pushed)==2,"binding 15 count 1 recorded");
  push(lod::internal::SLOTS,1,&replacing);push(lod::internal::SLOTS-1,2,pair.data());push(0,0,pair.data());
  Check(slots->game[lod::internal::SLOTS-1].Get()==native_pushed && slots->game[0].Get()==native_original && references(native_replacing)==3,"ranges past the last slot and empty ones change nothing");
  // Outside the main view (its end comes with a viewport bind) the game's own samplers are bound, and still recorded.
  lod::SetMainView([] {return false;});
  lod::OnBindViewports(&commands,0,0,nullptr);
  Check(bound_at(2).Get()==native_replacing,"the main view's end binds the game's samplers again");
  push(5,1,&pushed);
  Check(slots->game[5].Get()==native_pushed && bound_at(5).Get()==native_pushed,"outside the main view the game's sampler stays");
  lod::SetMainView([] {return true;});
  lod::OnBindViewports(&commands,0,0,nullptr);
  Check(bound_at(5).Get()==reinterpret_cast<ID3D11SamplerState*>(pushed_clone),"the main view binds the recorded slots as clones");
  // The game lets go of a sampler its slots still hold: the slots keep it alive under the bound clone.
  first.destroy_sampler(pushed);
  Check(references(native_pushed)==2 && bound_at(5).Get()==reinterpret_cast<ID3D11SamplerState*>(pushed_clone),"slots own a sampler the game released");
  lod::SetTracking(&queue,false);
  Check(bound_at(5).Get()==native_pushed && bound_at(2).Get()==native_replacing,"tracking off restores the pushed samplers");
  Microsoft::WRL::ComPtr<ID3D11SamplerState> bound;commands.native->PSGetSamplers(0,1,&bound);
  Check(bound.Get()==native_original,"tracking off restores original");bound.Reset();
  Check(lod::internal::tracking_devices.load()==1,"tracking off leaves the other device counted");
  commands.native->ClearState();
  lod::OnDestroyDevice(&first);
  Check(lod::SamplerCount()==1 && lod::internal::device_slots.size()==1,"device cleanup leaves other device intact");
  lod::Shutdown();
  Check(lod::SamplerCount()==0 && lod::internal::device_slots.size()==0,"shutdown clears all device caches");
  Check(lod::internal::tracking_devices.load()==0,"shutdown leaves no tracking device: events return on the count");
  other.native->PSGetSamplers(0,1,&bound);Check(bound.Get()==native_other,"shutdown restores other device original");bound.Reset();
  commands.native->ClearState();other.native->ClearState();
  puts("PASS: original ownership, duplicate identity, two-device isolation, per-bind recording (ranges, repeats, replacement, references, clones), tracking restore and shutdown cleanup");
}

