/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 *
 * The shader tests' WARP device, their HRESULT check, and their read and compile of the add-on's HLSL from its source
 * folder (nested includes as fxc resolves them).
 */

#pragma once

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

namespace warp {

using Microsoft::WRL::ComPtr;

inline void Check(HRESULT result) {
  if (FAILED(result)) throw std::runtime_error("Direct3D failed: " + std::to_string(result));
}

// Includes relative to the including file's folder, as fxc's (the standard handler takes nested ones from the top file's).
struct Includes : ID3DInclude {
  std::filesystem::path top;
  std::map<const void*, std::filesystem::path> folders;  // an opened file's text -> its folder
  std::vector<std::unique_ptr<std::string>> owned;
  HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID parent, LPCVOID* data, UINT* bytes) override {
    const auto found = folders.find(parent);
    const std::filesystem::path path = ((found != folders.end()) ? found->second : top) / name;
    std::ifstream file(path, std::ios::binary);
    if (!file) return E_FAIL;
    auto stored = std::make_unique<std::string>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    *data = stored->data();
    *bytes = static_cast<UINT>(stored->size());
    folders[*data] = path.parent_path();
    owned.push_back(std::move(stored));
    return S_OK;
  }
  HRESULT __stdcall Close(LPCVOID) override { return S_OK; }
};

// `text` compiled as `name` (its includes from `name`'s folder).
inline ComPtr<ID3DBlob> Compile(const std::string& text, const std::filesystem::path& name, const char* target) {
  Includes includes;
  includes.top = name.parent_path();
  ComPtr<ID3DBlob> code;
  ComPtr<ID3DBlob> errors;
  const HRESULT compiled = D3DCompile(text.data(), text.size(), name.string().c_str(), nullptr, &includes, "main", target, 0, 0,
                                      code.GetAddressOf(), errors.GetAddressOf());
  if (errors) {
    std::cerr << static_cast<const char*>(errors->GetBufferPointer());
  }
  Check(compiled);
  return code;
}

// A source file's text (a missing file fails here, not as an empty shader).
inline std::string Read(const std::filesystem::path& file) {
  std::ifstream input(file, std::ios::binary);
  if (!input) throw std::runtime_error("cannot open " + file.string());
  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

inline ComPtr<ID3DBlob> Compile(const std::filesystem::path& file, const char* target) {
  return Compile(Read(file), file, target);
}

inline ComPtr<ID3D11Device> Device(ComPtr<ID3D11DeviceContext>* context) {
  ComPtr<ID3D11Device> device;
  Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                          context->GetAddressOf()));
  return device;
}

}  // namespace warp
