#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <set>
#include <cmath>
#include <cstdio>
#include <limits>
using Microsoft::WRL::ComPtr;
void Check(bool good) { if (!good) { puts("FAIL"); ExitProcess(1); } }
int main() {
  ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
  Check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)));
  ComPtr<ID3DBlob> code,error;
  HRESULT hr=D3DCompileFromFile(L"f3-motion-test.hlsl",nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error);
  if(error) puts((char*)error->GetBufferPointer()); Check(SUCCEEDED(hr));
  ComPtr<ID3D11ComputeShader> shader; Check(SUCCEEDED(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader)));
  std::array<ComPtr<ID3D11Buffer>,2> buffers;
  std::array<ComPtr<ID3D11UnorderedAccessView>,2> views;
  for(unsigned i=0;i<2;++i) {
    D3D11_BUFFER_DESC desc={}; desc.ByteWidth=(i==0 ? 65536*8*4 : 65536*8*4*16); desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
    Check(SUCCEEDED(device->CreateBuffer(&desc,nullptr,&buffers[i])));
    D3D11_UNORDERED_ACCESS_VIEW_DESC view={}; view.Format=(i==0 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R32G32B32A32_UINT); view.ViewDimension=D3D11_UAV_DIMENSION_BUFFER; view.Buffer.NumElements=(i==0 ? 65536*8 : 65536*8*4);
    Check(SUCCEEDED(device->CreateUnorderedAccessView(buffers[i].Get(),&view,&views[i])));
  }
  D3D11_BUFFER_DESC desc={}; buffers[0]->GetDesc(&desc); desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Buffer> staging,constants; Check(SUCCEEDED(device->CreateBuffer(&desc,nullptr,&staging)));
  desc={}; desc.ByteWidth=16; desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER; Check(SUCCEEDED(device->CreateBuffer(&desc,nullptr,&constants)));
  context->CSSetShader(shader.Get(),nullptr,0); ID3D11UnorderedAccessView* uavs[]={views[0].Get(),views[1].Get()}; context->CSSetUnorderedAccessViews(0,2,uavs,nullptr);
  ID3D11Buffer* cb=constants.Get(); context->CSSetConstantBuffers(0,1,&cb);
  unsigned tested=0;
  for(float scale : {1.f,0.05f,0.f,-1.f}) for(float x : {-1000.5f,-1.f,-0.5f,0.f,0.025f,0.5f,1.f,1000.5f,16777216.f,-16777216.f,1e30f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
    const float input[]={x,x,scale,0}; const UINT clear[]={0,0,0,0}; context->ClearUnorderedAccessViewUint(views[0].Get(),clear); context->UpdateSubresource(constants.Get(),0,nullptr,input,0,0); context->Dispatch(1,1,1);
    context->CopyResource(staging.Get(),buffers[0].Get()); D3D11_MAPPED_SUBRESOURCE map={}; Check(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)));
    std::set<unsigned> expected;
    if(std::isfinite(x) && scale>0) {
      float low=std::floor((x-0.5f*scale)/scale), high=std::floor((x+0.5f*scale)/scale);
      if(std::isfinite(low) && std::isfinite(high) && std::abs(low)<=16777214.f && std::abs(high)<=16777214.f && high-low<=2) {
        for(float z=low;z<=high;++z) for(float v=low;v<=high;++v) expected.insert((((unsigned)(int)v*73856093u)^((unsigned)(int)z*19349663u)^(7u*83492791u))&65535u);
      }
    }
    auto* data=(unsigned*)map.pData; unsigned count=0;
    for(unsigned slot=0;slot<65536*8;++slot) if(data[slot]) { Check(data[slot]==1 && expected.contains(slot/8)); ++count; }
    Check(count==expected.size()); context->Unmap(staging.Get(),0); ++tested;
  }
  printf("PASS: %u actual MotionRecord WARP cases, normal cells identical; invalid/extreme inputs make no writes\n",tested);
}
