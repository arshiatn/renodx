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
// - The fix in ReShade's order (twattila/device_loss.hpp release_flip_chains_before_recreate; its own code on WARP:
//   test/tw_device_loss case 3c): destroy_swapchain comes before the chain's last release, so the views are unbound and
//   the context flushed there, and flushed again at the next create_swapchain.
// - The limit: a view of the old back buffer held elsewhere keeps the window, and neither the chain's nor the buffer's
//   reference count shows it, so no blt fallback can be chosen ahead of CreateSwapChain.
// - The window-mode switch (R1, 2026-11-16: a fresh factory, BufferCount 1, the one create that failed): a target the
//   game binds again after the destroy needs the views unbound at the create too; a view it keeps through the create is
//   the limit, and its own blt request works then (the fallback below ReShade: device_loss.hpp CreateSwapChainOrGameChain,
//   its own code on WARP: test/tw_device_loss case 3d), the next flip chain once it let go.
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

// The mod's flip chains: 2 buffers, scRGB. The game's blt chains: `blt_buffers`, 8-bit. `on`: a factory of the game's
// other than the first.
static HRESULT MakeChain(HWND window, DXGI_SWAP_EFFECT effect, IDXGISwapChain** chain, IDXGIFactory* on = nullptr, UINT blt_buffers = 1) {
  const bool flip = effect == DXGI_SWAP_EFFECT_FLIP_DISCARD || effect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  DXGI_SWAP_CHAIN_DESC desc = {
      .BufferDesc = {.Width = 64, .Height = 64, .Format = (flip ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM)},
      .SampleDesc = {.Count = 1},
      .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT,
      .BufferCount = (flip ? 2u : blt_buffers),
      .OutputWindow = window,
      .Windowed = TRUE,
      .SwapEffect = effect,
  };
  IDXGIFactory* const maker = (on != nullptr ? on : factory.Get());
  return maker->CreateSwapChain(device.Get(), &desc, chain);
}

// The fix's unbinding (mods::swapchain_v2 OnDestroySwapchain): the output merger's targets and every stage's textures
// and the compute UAVs; samplers, shaders and buffers stay.
static void UnbindViews() {
  ID3D11RenderTargetView* no_targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
  ID3D11ShaderResourceView* no_resources[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
  ID3D11UnorderedAccessView* no_uavs[D3D11_1_UAV_SLOT_COUNT] = {};
  context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, no_targets, nullptr);
  context->VSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, no_resources);
  context->HSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, no_resources);
  context->DSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, no_resources);
  context->GSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, no_resources);
  context->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, no_resources);
  context->CSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, no_resources);
  context->CSSetUnorderedAccessViews(0, D3D11_PS_CS_UAV_REGISTER_COUNT, no_uavs, nullptr);
}

// The game's last frame on a chain, then every reference the game holds goes. With `kept` (the fix, at ReShade's
// destroy_swapchain, before the chain's last release): the views unbound and the back buffer kept by one reference;
// with `flush_before_release` instead, the views unbound and the context flushed there (no reference kept).
static bool LeaveChain(HWND window, DXGI_SWAP_EFFECT effect, Left left, ID3D11Texture2D** kept = nullptr,
                       bool flush_before_release = false) {
  ComPtr<IDXGISwapChain> chain;
  ComPtr<ID3D11Texture2D> back_buffer;
  if (FAILED(MakeChain(window, effect, &chain)) || FAILED(chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer)))) return false;
  ComPtr<ID3D11RenderTargetView> target;
  if (FAILED(device->CreateRenderTargetView(back_buffer.Get(), nullptr, &target))) return false;
  constexpr FLOAT CLEAR[4] = {};
  context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
  context->ClearRenderTargetView(target.Get(), CLEAR);
  bool presented = false;
  switch (left) {
    case Left::TARGET_BEFORE_PRESENT:
      presented = SUCCEEDED(chain->Present(0, 0));
      break;
    case Left::TARGET_AFTER_PRESENT:
      presented = SUCCEEDED(chain->Present(0, 0));
      context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
      break;
    case Left::READ_AS_TEXTURE: {
      // D3D11 refuses a texture binding of a resource bound as a target: the target goes first.
      ID3D11RenderTargetView* no_target = nullptr;
      context->OMSetRenderTargets(1, &no_target, nullptr);
      ComPtr<ID3D11ShaderResourceView> resource;
      if (FAILED(device->CreateShaderResourceView(back_buffer.Get(), nullptr, &resource))) return false;
      context->PSSetShaderResources(0, 1, resource.GetAddressOf());
      presented = SUCCEEDED(chain->Present(0, 0));
      break;
    }
    case Left::VIEW_HELD:
      held_view = target;
      presented = SUCCEEDED(chain->Present(0, 0));
      break;
  }
  if (presented && (kept != nullptr || flush_before_release)) {
    UnbindViews();
  }
  if (presented && kept != nullptr) {
    *kept = back_buffer.Get();
    (*kept)->AddRef();
  }
  if (presented && flush_before_release) {
    context->Flush();
  }
  return presented;
}

// The fix in ReShade's order: views unbound and a Flush at destroy_swapchain (the back buffer's other references read
// there, as a fallback would have to), a Flush at the next create_swapchain, then the flip chain the mod asks for.
// `references_left`: the back buffer's references besides the one taken to read them.
static HRESULT RecreateWithFix(Left left, ULONG* references_left, const char* name) {
  HWND window = CreateWindowExW(0, L"STATIC", L"tw_d1_swapchain", WS_OVERLAPPEDWINDOW, 40, 40, 320, 200, nullptr, nullptr, nullptr, nullptr);
  ShowWindow(window, SW_SHOWNOACTIVATE);
  HRESULT result = E_FAIL;
  *references_left = ~0ul;
  ID3D11Texture2D* probe = nullptr;
  if (LeaveChain(window, DXGI_SWAP_EFFECT_FLIP_DISCARD, left, &probe, true)) {
    probe->AddRef();
    *references_left = probe->Release() - 1;
    probe->Release();
    context->Flush();
    ComPtr<IDXGISwapChain> chain;
    result = MakeChain(window, DXGI_SWAP_EFFECT_FLIP_DISCARD, &chain);
  }
  std::printf("%-80s 0x%08X, references left %lu\n", name, static_cast<unsigned>(result), *references_left);
  held_view.Reset();
  context->ClearState();
  context->Flush();
  DestroyWindow(window);
  return result;
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

static HRESULT Recreate(DXGI_SWAP_EFFECT first, Left left, Release release, DXGI_SWAP_EFFECT next, const char* name,
                        bool flush_before_release = false) {
  HWND window = CreateWindowExW(0, L"STATIC", L"tw_d1_swapchain", WS_OVERLAPPEDWINDOW, 40, 40, 320, 200, nullptr, nullptr, nullptr, nullptr);
  ShowWindow(window, SW_SHOWNOACTIVATE);
  HRESULT result = E_FAIL;
  if (LeaveChain(window, first, left, nullptr, flush_before_release)) {
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

// Divinity's window-mode switch (R1, 2026-11-16 01:46:43-45, the crash that is left): the mod's flip chain, the game's
// last frame on it (its target bound after Present), destroy_swapchain (the fix: views unbound and a Flush), the game's
// release, then a fresh CreateDXGIFactory1 and the game's BufferCount 1, windowed, DISCARD request, which the mod makes
// FLIP_DISCARD with 2 buffers. `gap`: what the game does with its view of the old back buffer before that create.
// `unbind_at_create`: the create's step unbinds the views as well as flushing (the fix; a Flush alone before). When the
// flip chain is refused, `fallback` gets the game's own request's result, then `after` the flip chain the game's next
// request gets once it let go of its view and of the fallback chain (S_OK in both when not asked).
enum class Gap { NOTHING, BOUND_AGAIN, VIEW_KEPT };
static HRESULT SwitchWindowMode(Gap gap, bool unbind_at_create, HRESULT* fallback, HRESULT* after, const char* name) {
  HWND window = CreateWindowExW(0, L"STATIC", L"tw_d1_swapchain", WS_OVERLAPPEDWINDOW, 40, 40, 320, 200, nullptr, nullptr, nullptr, nullptr);
  ShowWindow(window, SW_SHOWNOACTIVATE);
  HRESULT result = E_FAIL;
  *fallback = S_OK;
  *after = S_OK;
  ComPtr<ID3D11RenderTargetView> game_view;
  bool presented = false;
  {
    ComPtr<IDXGISwapChain> chain;
    ComPtr<ID3D11Texture2D> back_buffer;
    if (SUCCEEDED(MakeChain(window, DXGI_SWAP_EFFECT_FLIP_DISCARD, &chain)) && SUCCEEDED(chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer)))
        && SUCCEEDED(device->CreateRenderTargetView(back_buffer.Get(), nullptr, &game_view))) {
      context->OMSetRenderTargets(1, game_view.GetAddressOf(), nullptr);
      presented = SUCCEEDED(chain->Present(0, 0));
      context->OMSetRenderTargets(1, game_view.GetAddressOf(), nullptr);
      UnbindViews();
      context->Flush();
    }
  }
  switch (gap) {
    case Gap::NOTHING:
      game_view.Reset();
      break;
    case Gap::BOUND_AGAIN:
      context->OMSetRenderTargets(1, game_view.GetAddressOf(), nullptr);
      game_view.Reset();
      break;
    case Gap::VIEW_KEPT:
      break;
  }
  if (unbind_at_create) {
    UnbindViews();
  }
  context->Flush();
  ComPtr<IDXGIFactory1> fresh;
  if (presented && SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&fresh)))) {
    ComPtr<IDXGISwapChain> next;
    result = MakeChain(window, DXGI_SWAP_EFFECT_FLIP_DISCARD, &next, fresh.Get());
    if (result == E_ACCESSDENIED) {
      *fallback = MakeChain(window, DXGI_SWAP_EFFECT_DISCARD, &next, fresh.Get(), 1);
      game_view.Reset();
      next.Reset();
      UnbindViews();
      context->Flush();
      *after = MakeChain(window, DXGI_SWAP_EFFECT_FLIP_DISCARD, &next, fresh.Get());
    }
  }
  std::printf("%-80s 0x%08X, game's own 0x%08X, next 0x%08X\n", name, static_cast<unsigned>(result), static_cast<unsigned>(*fallback),
              static_cast<unsigned>(*after));
  game_view.Reset();
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

  // ReShade's order: destroy_swapchain comes before the chain's last release, so a Flush there is too early.
  {
    const HRESULT early = Recreate(FLIP, Left::TARGET_AFTER_PRESENT, Release::NOTHING, FLIP,
                                   "flip -> flip, target after Present, unbind + Flush before the release only", true);
    std::printf("  (the create-time Flush is %s)\n", (SUCCEEDED(early) ? "not needed on WARP" : "needed"));
  }
  // The fix in ReShade's order (twattila/device_loss.hpp release_flip_chains_before_recreate): unbind and Flush at
  // destroy_swapchain, Flush at the next create_swapchain.
  {
    ULONG references = 0;
    CHECK(SUCCEEDED(RecreateWithFix(Left::TARGET_BEFORE_PRESENT, &references, "fix: target bound before Present")));
    CHECK(references == 0);
    CHECK(SUCCEEDED(RecreateWithFix(Left::TARGET_AFTER_PRESENT, &references, "fix: target bound after Present (the crash)")));
    CHECK(references == 0);
    CHECK(SUCCEEDED(RecreateWithFix(Left::READ_AS_TEXTURE, &references, "fix: read as texture")));
    CHECK(references == 0);
    // The limit: a view another holder keeps still takes the window, and the buffer's count says nothing of it.
    CHECK(RecreateWithFix(Left::VIEW_HELD, &references, "fix: a view held elsewhere (the limit)") == E_ACCESSDENIED);
    CHECK(references == 0);
  }

  // The window-mode switch: the fresh factory and the game's BufferCount 1 change nothing by themselves; the game's target
  // bound again after the destroy needs the create's unbinding (the control: a Flush alone), and a view it keeps through
  // the create keeps the window from every flip chain: its own blt request is made, and the next flip chain once it let go.
  {
    HRESULT fallback = S_OK;
    HRESULT after = S_OK;
    CHECK(SUCCEEDED(SwitchWindowMode(Gap::NOTHING, false, &fallback, &after, "switch: fresh factory, BufferCount 1, nothing in between")));
    CHECK(SwitchWindowMode(Gap::BOUND_AGAIN, false, &fallback, &after, "switch: target bound again, Flush at create (before)") == E_ACCESSDENIED);
    CHECK(SUCCEEDED(SwitchWindowMode(Gap::BOUND_AGAIN, true, &fallback, &after, "switch: target bound again, unbind + Flush at create (fix)")));
    CHECK(SwitchWindowMode(Gap::VIEW_KEPT, false, &fallback, &after, "switch: view kept by the game, Flush at create (R1 crash)") == E_ACCESSDENIED);
    CHECK(SwitchWindowMode(Gap::VIEW_KEPT, true, &fallback, &after, "switch: view kept by the game, unbind + Flush at create") == E_ACCESSDENIED);
    CHECK(SUCCEEDED(fallback));
    CHECK(SUCCEEDED(after));
  }

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
