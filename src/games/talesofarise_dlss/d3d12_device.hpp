/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_1.h>
#include <d3d12.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>

#include <string>

// A D3D12 device beside the game's D3D11 one (ray tracing, the frame generation presenter), and D3D11 textures opened
// in it. As Phantom FX by Bustanity (MIT): d3d12.dll is loaded only when needed, and ReShade, which wraps a D3D12
// device it sees made (other add-ons would take it for a second game), hands out its native one for
// IID_UnwrappedObject.
namespace talesofarise::d3d12_device {

using Microsoft::WRL::ComPtr;

// ReShade's proxies hand out their native object for this interface (IID_UnwrappedObject).
constexpr GUID UNWRAPPED_OBJECT = {0x7f2c9a11, 0x3b4e, 0x4d6a, {0x81, 0x2f, 0x5e, 0x9c, 0xd3, 0x7a, 0x1b, 0x42}};

struct Device {
  ComPtr<ID3D12Device> wrapper;  // ReShade's proxy (when it wraps), kept alive while `native` is used
  ComPtr<ID3D12Device> native;
  PFN_D3D12_SERIALIZE_ROOT_SIGNATURE serialize_root_signature = nullptr;
};

// On `device11`'s GPU; an empty string when made, else why not.
inline std::string Create(ID3D11Device* device11, Device* device) {
  HMODULE d3d12 = LoadLibraryW(L"d3d12.dll");
  const auto create_device = reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(
      (d3d12 != nullptr) ? GetProcAddress(d3d12, "D3D12CreateDevice") : nullptr);
  device->serialize_root_signature = reinterpret_cast<PFN_D3D12_SERIALIZE_ROOT_SIGNATURE>(
      (d3d12 != nullptr) ? GetProcAddress(d3d12, "D3D12SerializeRootSignature") : nullptr);
  if (create_device == nullptr || device->serialize_root_signature == nullptr) return "D3D12 is not available.";
  ComPtr<IDXGIDevice> dxgi_device;
  ComPtr<IDXGIAdapter> adapter;
  if (FAILED(device11->QueryInterface(IID_PPV_ARGS(dxgi_device.GetAddressOf())))
      || FAILED(dxgi_device->GetAdapter(adapter.GetAddressOf()))) {
    return "no GPU found.";
  }
  if (FAILED(create_device(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device->wrapper.GetAddressOf())))) {
    return "D3D12 device creation failed.";
  }
  if (FAILED(device->wrapper->QueryInterface(UNWRAPPED_OBJECT, reinterpret_cast<void**>(device->native.GetAddressOf())))) {
    device->native = device->wrapper;
  }
  return {};
}

// The Agility SDK version shipped in the add-on's D3D12 folder (D3D12Core.dll, Microsoft's redistributable).
constexpr UINT AGILITY_SDK_VERSION = 619;
constexpr GUID SDK_CONFIGURATION = {0x7cda6aca, 0xa03e, 0x49c8, {0x94, 0x58, 0x03, 0x34, 0xd2, 0x0e, 0x07, 0xce}};

// As Create, with the Agility SDK's D3D12Core.dll from `folder` (DXR 1.2: opacity micromaps and shader execution
// reordering, shader model 6.9) through a device factory of its own: no developer mode, no exports in the game's exe,
// and the process's other D3D12 devices (frame generation) keep Windows' runtime. ReShade does not see this device.
// An empty string when made, else why not (the caller then uses Create).
inline std::string CreateAgility(ID3D11Device* device11, const std::string& folder, Device* device) {
  HMODULE d3d12 = LoadLibraryW(L"d3d12.dll");
  const auto get_interface = reinterpret_cast<PFN_D3D12_GET_INTERFACE>(
      (d3d12 != nullptr) ? GetProcAddress(d3d12, "D3D12GetInterface") : nullptr);
  device->serialize_root_signature = reinterpret_cast<PFN_D3D12_SERIALIZE_ROOT_SIGNATURE>(
      (d3d12 != nullptr) ? GetProcAddress(d3d12, "D3D12SerializeRootSignature") : nullptr);
  if (get_interface == nullptr || device->serialize_root_signature == nullptr) return "D3D12GetInterface is not available.";
  ComPtr<IDXGIDevice> dxgi_device;
  ComPtr<IDXGIAdapter> adapter;
  ComPtr<ID3D12SDKConfiguration1> configuration;
  ComPtr<ID3D12DeviceFactory> factory;
  if (FAILED(device11->QueryInterface(IID_PPV_ARGS(dxgi_device.GetAddressOf())))
      || FAILED(dxgi_device->GetAdapter(adapter.GetAddressOf()))) {
    return "no GPU found.";
  }
  if (FAILED(get_interface(SDK_CONFIGURATION, IID_PPV_ARGS(configuration.GetAddressOf())))
      || FAILED(configuration->CreateDeviceFactory(AGILITY_SDK_VERSION, folder.c_str(), IID_PPV_ARGS(factory.GetAddressOf())))) {
    return "the Agility SDK did not load.";
  }
  factory->SetFlags(D3D12_DEVICE_FACTORY_FLAG_DISALLOW_STORING_NEW_DEVICE_AS_SINGLETON);
  if (FAILED(factory->CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device->native.GetAddressOf())))) {
    return "the Agility SDK device creation failed.";
  }
  device->wrapper = device->native;
  return {};
}

// `texture` (made with D3D11_RESOURCE_MISC_SHARED_NTHANDLE) opened in `device`.
inline HRESULT Open(ID3D12Device* device, ID3D11Texture2D* texture, ComPtr<ID3D12Resource>* opened) {
  ComPtr<IDXGIResource1> resource;
  HANDLE handle = nullptr;
  HRESULT result = texture->QueryInterface(IID_PPV_ARGS(resource.GetAddressOf()));
  if (SUCCEEDED(result)) {
    result = resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &handle);
  }
  if (FAILED(result)) return result;
  result = device->OpenSharedHandle(handle, IID_PPV_ARGS(opened->ReleaseAndGetAddressOf()));
  CloseHandle(handle);
  return result;
}

}  // namespace talesofarise::d3d12_device
