// Divinity: Original Sin EE's start crash (2026-11-15, R0 18:04:11 and the retry 18:11:18) on WARP: the game recreates
// its swap chain on one device and one window (SetFullscreenState FALSE, last Release, CreateSwapChain), 2 - 4 times a
// start and at every window-mode change. It asks for a blt chain (DISCARD, 1 - 2 buffers); the swapchain mod makes it
// FLIP_DISCARD with 2 buffers. ReShade logs "Reference count for IDXGISwapChain0 ... is inconsistent (1)" at each
// release: the chain outlives the game's last Release. A flip chain kept alive keeps its window, and the next flip chain
// is refused with E_ACCESSDENIED: the game then reads a null resource (EoCApp+0xF7C532).
// Each case makes a chain, leaves its back buffer the way the last frame did, drops the test's own references, lets go
// of what the case names, and asks for the next chain for the same window. Measured on WARP (2026-11-15):
// - A flip Present unbinds the back buffer from the output merger: a target bound before the last Present holds nothing.
// - A target bound after it (the next frame's, set before the release), the back buffer read as a texture, or a view of
//   it still referenced keep the window: E_ACCESSDENIED. A Flush alone frees none of them.
// - Blt chains never hold their window: the game alone never sees this; a blt chain is also made while a flip chain
//   still holds the window (the fallback: the game's own swap effect, SDR for that chain).
// - ClearState drops the game's other state and still misses a view referenced elsewhere: the fix unbinds the views,
//   releases its own, and flushes.
// tests.exe; exit code 0 when every check holds.
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <cstdio>

using Microsoft::WRL::ComPtr;

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (false)

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;
static ComPtr<IDXGIFactory1> factory;
// A view of the last back buffer that something other than the game still references (a cache of views).
static ComPtr<ID3D11RenderTargetView> held_view;

enum class Left { TARGET_BEFORE_PRESENT, TARGET_AFTER_PRESENT, READ_AS_TEXTURE, VIEW_HELD };
enum class Release { NOTHING, FLUSH, UNBIND_TARGETS_AND_FLUSH, UNBIND_ALL_AND_FLUSH, CLEAR_STATE_AND_FLUSH, DROP_VIEW_AND_FLUSH };

static HRESULT MakeChain(HWND window, DXGI_SWAP_EFFECT effect, IDXGISwapChain** chain) {
  const bool flip = effect == DXGI_SWAP_EFFECT_FLIP_DISCARD || effect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  DXGI_SWAP_CHAIN_DESC desc = {
      .BufferDesc = {.Width = 64, .Height = 64, .Format = (flip ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM)},
      .SampleDesc = {.Count = 1},
      .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT,
      .BufferCount = (flip ? 2u : 1u),
      .OutputWindow = window,
      .Windowed = TRUE,
      .SwapEffect = effect,
  };
  return factory->CreateSwapChain(device.Get(), &desc, chain);
}

// The game's last frame on a chain, then every reference the game holds goes.
static bool LeaveChain(HWND window, DXGI_SWAP_EFFECT effect, Left left) {
  ComPtr<IDXGISwapChain> chain;
  ComPtr<ID3D11Texture2D> back_buffer;
  if (FAILED(MakeChain(window, effect, &chain)) || FAILED(chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer)))) return false;
  ComPtr<ID3D11RenderTargetView> target;
  if (FAILED(device->CreateRenderTargetView(back_buffer.Get(), nullptr, &target))) return false;
  constexpr FLOAT CLEAR[4] = {};
  context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
  context->ClearRenderTargetView(target.Get(), CLEAR);
  switch (left) {
    case Left::TARGET_BEFORE_PRESENT:
      return SUCCEEDED(chain->Present(0, 0));
    case Left::TARGET_AFTER_PRESENT:
      if (FAILED(chain->Present(0, 0))) return false;
      context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
      return true;
    case Left::READ_AS_TEXTURE: {
      // D3D11 refuses a texture binding of a resource bound as a target: the target goes first.
      ID3D11RenderTargetView* no_target = nullptr;
      context->OMSetRenderTargets(1, &no_target, nullptr);
      ComPtr<ID3D11ShaderResourceView> resource;
      if (FAILED(device->CreateShaderResourceView(back_buffer.Get(), nullptr, &resource))) return false;
      context->PSSetShaderResources(0, 1, resource.GetAddressOf());
      return SUCCEEDED(chain->Present(0, 0));
    }
    case Left::VIEW_HELD:
      held_view = target;
      return SUCCEEDED(chain->Present(0, 0));
  }
  return false;
}

static void LetGo(Release release) {
  ID3D11RenderTargetView* no_targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
  ID3D11ShaderResourceView* no_resources[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
  switch (release) {
    case Release::NOTHING:
      break;
    case Release::FLUSH:
      context->Flush();
      break;
    case Release::UNBIND_TARGETS_AND_FLUSH:
      context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, no_targets, nullptr);
      context->Flush();
      break;
    case Release::UNBIND_ALL_AND_FLUSH:
      context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, no_targets, nullptr);
      context->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, no_resources);
      context->Flush();
      break;
    case Release::CLEAR_STATE_AND_FLUSH:
      context->ClearState();
      context->Flush();
      break;
    case Release::DROP_VIEW_AND_FLUSH:
      held_view.Reset();
      context->Flush();
      break;
  }
}

static HRESULT Recreate(DXGI_SWAP_EFFECT first, Left left, Release release, DXGI_SWAP_EFFECT next, const char* name) {
  HWND window = CreateWindowExW(0, L"STATIC", L"tw_d1_swapchain", WS_OVERLAPPEDWINDOW, 40, 40, 320, 200, nullptr, nullptr, nullptr, nullptr);
  ShowWindow(window, SW_SHOWNOACTIVATE);
  HRESULT result = E_FAIL;
  if (LeaveChain(window, first, left)) {
    LetGo(release);
    ComPtr<IDXGISwapChain> chain;
    result = MakeChain(window, next, &chain);
  }
  std::printf("%-80s 0x%08X\n", name, static_cast<unsigned>(result));
  // Every case starts from a context without the last case's bindings and a window of its own.
  held_view.Reset();
  context->ClearState();
  context->Flush();
  DestroyWindow(window);
  return result;
}

int main() {
  // The game's device: D3D11CreateDevice with SINGLETHREADED (ReShade.log 2026-10-17).
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_SINGLETHREADED, nullptr, 0,
                               D3D11_SDK_VERSION, &device, nullptr, &context))
      || FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    std::printf("FAIL: no WARP device\n");
    return 1;
  }
  constexpr auto BLT = DXGI_SWAP_EFFECT_DISCARD;
  constexpr auto FLIP = DXGI_SWAP_EFFECT_FLIP_DISCARD;

  // The game alone: blt chains never hold their window.
  CHECK(SUCCEEDED(Recreate(BLT, Left::TARGET_AFTER_PRESENT, Release::NOTHING, BLT, "blt -> blt, target bound after Present (the game alone)")));
  CHECK(SUCCEEDED(Recreate(BLT, Left::READ_AS_TEXTURE, Release::NOTHING, BLT, "blt -> blt, read as texture")));
  CHECK(SUCCEEDED(Recreate(BLT, Left::VIEW_HELD, Release::NOTHING, BLT, "blt -> blt, a view held")));
  // A flip Present unbinds the output merger: a target bound before it holds nothing.
  CHECK(SUCCEEDED(Recreate(FLIP, Left::TARGET_BEFORE_PRESENT, Release::NOTHING, FLIP, "flip -> flip, target bound before Present")));
  // Today with the mod (the controls: must fail), and a Flush alone frees none of them.
  CHECK(Recreate(FLIP, Left::TARGET_AFTER_PRESENT, Release::NOTHING, FLIP, "flip -> flip, target bound after Present (today)") == E_ACCESSDENIED);
  CHECK(Recreate(FLIP, Left::READ_AS_TEXTURE, Release::NOTHING, FLIP, "flip -> flip, read as texture (today)") == E_ACCESSDENIED);
  CHECK(Recreate(FLIP, Left::VIEW_HELD, Release::NOTHING, FLIP, "flip -> flip, a view held (today)") == E_ACCESSDENIED);
  CHECK(Recreate(FLIP, Left::TARGET_AFTER_PRESENT, Release::FLUSH, FLIP, "flip -> flip, target bound after Present, Flush only") == E_ACCESSDENIED);
  // The fix: every pipeline binding of the old chain dropped and a Flush; views held elsewhere released first.
  CHECK(SUCCEEDED(Recreate(FLIP, Left::TARGET_AFTER_PRESENT, Release::UNBIND_TARGETS_AND_FLUSH, FLIP, "flip -> flip, target after Present, targets unbound + Flush")));
  CHECK(Recreate(FLIP, Left::READ_AS_TEXTURE, Release::UNBIND_TARGETS_AND_FLUSH, FLIP, "flip -> flip, read as texture, targets unbound + Flush") == E_ACCESSDENIED);
  CHECK(SUCCEEDED(Recreate(FLIP, Left::READ_AS_TEXTURE, Release::UNBIND_ALL_AND_FLUSH, FLIP, "flip -> flip, read as texture, targets + textures unbound + Flush")));
  CHECK(SUCCEEDED(Recreate(FLIP, Left::READ_AS_TEXTURE, Release::CLEAR_STATE_AND_FLUSH, FLIP, "flip -> flip, read as texture, ClearState + Flush")));
  CHECK(Recreate(FLIP, Left::VIEW_HELD, Release::CLEAR_STATE_AND_FLUSH, FLIP, "flip -> flip, a view held, ClearState + Flush") == E_ACCESSDENIED);
  CHECK(SUCCEEDED(Recreate(FLIP, Left::VIEW_HELD, Release::DROP_VIEW_AND_FLUSH, FLIP, "flip -> flip, a view held, view released + Flush")));
  // The game's own blt chain as the fallback works while a flip chain still holds the window.
  CHECK(SUCCEEDED(Recreate(FLIP, Left::TARGET_AFTER_PRESENT, Release::NOTHING, BLT, "flip -> blt, target bound after Present (fallback)")));
  CHECK(SUCCEEDED(Recreate(FLIP, Left::VIEW_HELD, Release::NOTHING, BLT, "flip -> blt, a view held (fallback)")));

  // ClearState drops the game's other state (a sampler it set once); unbinding the views keeps it.
  {
    const D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                             .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                             .MaxLOD = D3D11_FLOAT32_MAX};
    ComPtr<ID3D11SamplerState> sampler;
    CHECK(SUCCEEDED(device->CreateSamplerState(&sampler_desc, &sampler)));
    context->PSSetSamplers(0, 1, sampler.GetAddressOf());
    LetGo(Release::UNBIND_ALL_AND_FLUSH);
    ComPtr<ID3D11SamplerState> kept;
    context->PSGetSamplers(0, 1, &kept);
    CHECK(kept.Get() == sampler.Get());
    LetGo(Release::CLEAR_STATE_AND_FLUSH);
    ComPtr<ID3D11SamplerState> cleared;
    context->PSGetSamplers(0, 1, &cleared);
    CHECK(cleared == nullptr);
  }

  std::printf(failures == 0 ? "tw_d1_swapchain: all checks passed\n" : "tw_d1_swapchain: %d checks failed\n", failures);
  return failures == 0 ? 0 : 1;
}
