/* SPDX-License-Identifier: MIT */
#pragma once

#include <d3d11_1.h>
#include <wrl/client.h>

namespace talesofarise::dlss {

// UE caches bindings outside D3D11. Every native SDK call must leave the
// complete original state intact, including non-compute and ranged CB state.
struct ContextStateScope {
  ID3D11DeviceContext1* context;
  Microsoft::WRL::ComPtr<ID3DDeviceContextState> previous;

  // `replacement` must be clear: it is created that way and cleared again on every exit.
  ContextStateScope(ID3D11DeviceContext1* context, ID3DDeviceContextState* replacement)
      : context(context) {
    context->SwapDeviceContextState(replacement, previous.GetAddressOf());
  }

  ContextStateScope(const ContextStateScope&) = delete;
  ContextStateScope& operator=(const ContextStateScope&) = delete;

  ~ContextStateScope() {
    context->ClearState();
    context->SwapDeviceContextState(previous.Get(), nullptr);
  }
};

// A clear context state for ContextStateScope on `device` (single-threaded where the device is).
inline bool MakeContextState(ID3D11Device* device, Microsoft::WRL::ComPtr<ID3DDeviceContextState>* state) {
  Microsoft::WRL::ComPtr<ID3D11Device1> device1;
  const D3D_FEATURE_LEVEL feature_level = device->GetFeatureLevel();
  const UINT flags = ((device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0)
                         ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
                         : 0;
  return SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(device1.GetAddressOf())))
         && SUCCEEDED(device1->CreateDeviceContextState(flags, &feature_level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1),
                                                        nullptr, state->ReleaseAndGetAddressOf()));
}

// A constant buffer as the game bound it, to bind again in the add-on's own context state: its first constant and count
// (a whole-buffer binding reads back as 0 and 4096).
struct ConstantBinding {
  Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
  UINT first = 0;
  UINT count = 0;
};

}  // namespace talesofarise::dlss
