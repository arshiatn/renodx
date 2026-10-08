/* SPDX-License-Identifier: MIT */
#pragma once

#include <d3d11_1.h>
#include <wrl/client.h>

namespace expeditionsrome::dlss {

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

}  // namespace expeditionsrome::dlss
