/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// F7: the 64-bit Attila loader's present path, as attila64.exe next to ReShade (dxgi.dll), the add-on and
// zz_capture.addon64 (run.ps1 stages and checks the logs). Steps in the loader's order:
// 1. 0x400000 + 16 MB reserved no-access; a first vectored handler counts access violations inside it (any = fail).
// 2. A probe device made and released: every add-on loaded and unloaded, its image freed.
// 3. The loader's vectored handler and unhandled filter; 0x400000 committed with a synthetic ELF64 header (image_none:
//    left reserved; image_badmagic: a wrong magic; image_table: a program header table past the page).
// 3b. A plugin's test device (made and released): the same asserts.
// 4. Window, device, flip swap chain (RGBA8, 2 buffers), the picture (RGBA8, SRV | RT) and its depth.
// 5-7. Frames into the picture, the loader's copy into buffer 0 and its 8-bit overlay, a mode change, a new window.
// Then the captures (zz_capture) against the reference of the swap chain proxy.
// Usage: attila64.exe d3d11 <1320|fixed> <upgraded|plain|broken|no_overlay> [pitch8] [image_none|image_badmagic|image_table] [gpu]
//        attila64.exe d3d12 [gpu]   (C4: frames written; run.ps1 compares them with a run without the add-on)
//        attila64.exe opengl        (C6: as C4, GPU only)

#include <windows.h>

#include <d3d11.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "src/utils/float16.hpp"

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace {

constexpr uintptr_t GAME_BASE = 0x400000;
constexpr size_t GAME_RESERVE = 16u << 20;
constexpr size_t GAME_COMMIT = 64u << 10;
constexpr std::array<uint8_t, 20> TEST_BUILD_ID = {0xA6, 0x4B, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                                   0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11};

std::atomic<uint32_t> reserved_range_violations = 0;
std::vector<std::string> failures;
fs::path here;

void Fail(const std::string& what) {
  failures.push_back(what);
  std::cout << "FAIL: " << what << '\n';
}

LONG CALLBACK CountReservedRangeViolations(EXCEPTION_POINTERS* info) {
  const auto* record = info->ExceptionRecord;
  if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
    const uintptr_t address = record->ExceptionInformation[1];
    if (address >= GAME_BASE && address < GAME_BASE + GAME_RESERVE) {
      reserved_range_violations.fetch_add(1);
    }
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

// The loader's own handler and filter (installed after its probe device): they only pass exceptions on.
LONG CALLBACK LoaderCrashFilter(EXCEPTION_POINTERS*) { return EXCEPTION_CONTINUE_SEARCH; }
LONG WINAPI LoaderUnhandledNote(EXCEPTION_POINTERS*) { return EXCEPTION_CONTINUE_SEARCH; }

std::string ReadShared(const fs::path& path) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return {};
  std::string text;
  char buffer[65536];
  DWORD read = 0;
  while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read != 0) {
    text.append(buffer, read);
  }
  CloseHandle(file);
  return text;
}

size_t Count(std::string_view text, std::string_view what) {
  size_t count = 0;
  for (size_t at = text.find(what); at != std::string_view::npos; at = text.find(what, at + what.size())) {
    ++count;
  }
  return count;
}

std::vector<std::wstring> AddonFiles() {
  std::vector<std::wstring> files;
  for (const auto& entry : fs::directory_iterator(here)) {
    if (entry.path().extension() == L".addon64") {
      files.push_back(entry.path().filename().wstring());
    }
  }
  return files;
}

// After a device made and released before the game's: each add-on loaded and unloaded `cycles` times, and freed.
void CheckAddonsFreed(const char* step, size_t cycles) {
  const std::string log = ReadShared(here / "ReShade.log");
  const auto files = AddonFiles();
  for (const auto& file : files) {
    const std::string name = fs::path(file).string();
    const size_t loads = Count(log, name + "' ...");
    if (loads != cycles) Fail(std::format("{}: {} loaded {} times (expected {})", step, name, loads, cycles));
    if (GetModuleHandleW(file.c_str()) != nullptr) Fail(std::format("{}: {} still in memory after the device's release", step, name));
  }
  const size_t unloads = Count(log, "Unloading add-on \"");
  if (unloads != files.size() * cycles) Fail(std::format("{}: {} add-on unloads (expected {})", step, unloads, files.size() * cycles));
  std::cout << step << ": " << files.size() << " add-ons loaded / unloaded " << cycles << "x, freed\n";
}

void CommitSyntheticElf(bool bad_magic, bool bad_table) {
  auto* base = static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(GAME_BASE), GAME_COMMIT, MEM_COMMIT, PAGE_READWRITE));
  if (base == nullptr) {
    Fail("commit of 0x400000 failed");
    return;
  }
  const auto put16 = [base](size_t at, uint16_t value) { std::memcpy(base + at, &value, 2); };
  const auto put32 = [base](size_t at, uint32_t value) { std::memcpy(base + at, &value, 4); };
  const auto put64 = [base](size_t at, uint64_t value) { std::memcpy(base + at, &value, 8); };
  constexpr uint8_t IDENT[] = {0x7F, 'E', 'L', 'F', 2, 1, 1, 0};
  std::memcpy(base, IDENT, sizeof(IDENT));
  put16(16, 2);       // e_type: executable
  put16(18, 0x3E);    // e_machine: x86-64
  put32(20, 1);       // e_version
  put64(24, GAME_BASE + 0x1000);
  put64(32, 64);      // e_phoff
  put16(52, 64);      // e_ehsize
  put16(54, 56);      // e_phentsize
  put16(56, bad_table ? 0xFFFF : 2);  // e_phnum (0xFFFF: 3.6 MB of headers)
  // PT_LOAD, R + X, the whole committed range.
  put32(64, 1);
  put32(68, 5);
  put64(72, 0);
  put64(80, GAME_BASE);
  put64(88, GAME_BASE);
  put64(96, GAME_COMMIT);
  put64(104, GAME_COMMIT);
  put64(112, 0x1000);
  // PT_NOTE at +0x274: the build-id note.
  constexpr size_t NOTE = 0x274;
  put32(120, 4);
  put32(124, 4);
  put64(128, NOTE);
  put64(136, GAME_BASE + NOTE);
  put64(144, GAME_BASE + NOTE);
  put64(152, 12 + 4 + TEST_BUILD_ID.size());
  put64(160, 12 + 4 + TEST_BUILD_ID.size());
  put64(168, 4);
  put32(NOTE, 4);
  put32(NOTE + 4, static_cast<uint32_t>(TEST_BUILD_ID.size()));
  put32(NOTE + 8, 3);  // NT_GNU_BUILD_ID
  std::memcpy(base + NOTE + 12, "GNU", 4);
  std::memcpy(base + NOTE + 16, TEST_BUILD_ID.data(), TEST_BUILD_ID.size());
  if (bad_magic) base[3] = 'G';
  DWORD old_protect = 0;
  VirtualProtect(base, GAME_COMMIT, PAGE_EXECUTE_READ, &old_protect);
}

HWND MakeWindow(uint32_t width, uint32_t height) {
  static bool registered = false;
  if (!registered) {
    const WNDCLASSW window_class = {.lpfnWndProc = DefWindowProcW, .hInstance = GetModuleHandleW(nullptr), .lpszClassName = L"attila64_present"};
    RegisterClassW(&window_class);
    registered = true;
  }
  RECT rect = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
  HWND window = CreateWindowW(L"attila64_present", L"attila64 present repro", WS_OVERLAPPEDWINDOW, 40, 40, rect.right - rect.left,
                              rect.bottom - rect.top, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  ShowWindow(window, SW_SHOWNOACTIVATE);
  return window;
}

void PumpMessages() {
  MSG message;
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
}

// The picture the game draws: three primaries in the top fifth, a grey ramp 0..1 (left half) and 1..4 (right half: above
// SDR white, kept only by an upgraded picture).
constexpr char PICTURE_SHADER[] = R"(
cbuffer size : register(b0) { float2 size; };
float4 vs(uint id : SV_VertexID) : SV_Position {
  float2 uv = float2((id << 1) & 2, id & 2);
  return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
float4 ps(float4 position : SV_Position) : SV_Target {
  float u = position.x / size.x;
  float v = position.y / size.y;
  if (v < 0.2f) {
    if (u < 1.f / 3.f) return float4(1, 0, 0, 1);
    if (u < 2.f / 3.f) return float4(0, 1, 0, 1);
    return float4(0, 0, 1, 1);
  }
  float grey = (u < 0.5f) ? (u * 2.f) : (1.f + (u - 0.5f) * 6.f);
  return float4(grey, grey, grey, 1);
}
)";

std::array<float, 3> PictureValue(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
  const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
  const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
  if (v < 0.2f) {
    if (u < 1.f / 3.f) return {1.f, 0.f, 0.f};
    if (u < 2.f / 3.f) return {0.f, 1.f, 0.f};
    return {0.f, 0.f, 1.f};
  }
  const float grey = (u < 0.5f) ? (u * 2.f) : (1.f + (u - 0.5f) * 6.f);
  return {grey, grey, grey};
}

// The loader's frame-rate box and Discord button: 8-bit rows, pitch 4 x width (8 x width for C3).
struct OverlayBox {
  uint32_t x = 0;
  uint32_t y = 0;
  uint32_t width = 0;
  uint32_t height = 0;
};

std::array<OverlayBox, 2> OverlayBoxes(uint32_t width, uint32_t height) {
  return {{{.x = 16, .y = 16, .width = 96, .height = 24}, {.x = width - 80, .y = height - 80, .width = 64, .height = 64}}};
}

std::array<uint8_t, 4> OverlayPixel(size_t box, uint32_t i, uint32_t j) {
  return {static_cast<uint8_t>((i * 5 + box * 40) & 255), static_cast<uint8_t>((j * 9 + 30) & 255),
          static_cast<uint8_t>(200 - box * 60), 255};
}

std::optional<size_t> OverlayAt(const std::array<OverlayBox, 2>& boxes, uint32_t x, uint32_t y) {
  for (size_t box = 0; box < boxes.size(); ++box) {
    if (x >= boxes[box].x && x < boxes[box].x + boxes[box].width && y >= boxes[box].y && y < boxes[box].y + boxes[box].height) {
      return box;
    }
  }
  return std::nullopt;
}

// The add-on's settings this run wrote (ReShade.ini, [renodx] and [renodx-preset1]).
struct ProxySettings {
  int encoding = 0;  // 0 HDR10, 1 scRGB, 2 SDR
  float rendering_mode = 3.f;
  float peak = 1000.f;
  float paper_white = 203.f;
  float ui = 203.f;
  float gamma_correction = 1.f;
};

float IniFloat(const wchar_t* section, const wchar_t* key, float fallback) {
  wchar_t value[64] = {};
  GetPrivateProfileStringW(section, key, L"", value, 64, (here / "ReShade.ini").c_str());
  return (value[0] != 0) ? std::wcstof(value, nullptr) : fallback;
}

ProxySettings ReadProxySettings() {
  return {
      .encoding = static_cast<int>(IniFloat(L"renodx", L"SwapChainEncoding", 0.f)),
      .rendering_mode = IniFloat(L"renodx-preset1", L"tonemapper", 3.f),
      .peak = IniFloat(L"renodx-preset1", L"peak_white_nits", 1000.f),
      .paper_white = IniFloat(L"renodx-preset1", L"diffuse_white_nits", 203.f),
      .ui = IniFloat(L"renodx-preset1", L"graphics_white_nits", 203.f),
      .gamma_correction = IniFloat(L"renodx-preset1", L"gamma_correction", 1.f),
  };
}

float SignPow(float value, float exponent) { return std::copysign(std::pow(std::abs(value), exponent), value); }

// twattila's swap_chain_proxy_pixel_shader.ps_5_x.hlsl on one pixel of the frame (RGBA16F), for the setting's output:
// gamma = 2.2 when Gamma Correction is on (2.0 off; SDR with Rendering Mode SDR the other way round);
// SDR at gamma 2.2: saturate(x) as it is; SDR else: encode 2.2 of saturate(decode(x)).
// HDR: y = BT.2020 of decode(x), clamped at 0, scaled down by its largest channel to Peak / UI (Rendering Mode SDR:
// max(Paper White, UI) / UI); HDR10 PQ of y x UI / 10000, scRGB BT.709 of y x UI / 80.
std::array<float, 3> Proxy(std::array<float, 3> x, const ProxySettings& settings) {
  const bool sdr = (settings.encoding == 2);
  const float gamma = (((settings.gamma_correction != 0.f) != (sdr && settings.rendering_mode < 0.5f)) ? 2.2f : 2.f);
  if (sdr && gamma == 2.2f) {
    return {std::clamp(x[0], 0.f, 1.f), std::clamp(x[1], 0.f, 1.f), std::clamp(x[2], 0.f, 1.f)};
  }
  for (float& channel : x) {
    channel = SignPow(channel, gamma);
  }
  if (sdr) {
    for (float& channel : x) {
      channel = SignPow(std::clamp(channel, 0.f, 1.f), 1.f / 2.2f);
    }
    return x;
  }
  constexpr float TO_BT2020[3][3] = {{0.6274040f, 0.3292820f, 0.0433136f}, {0.0690970f, 0.9195400f, 0.0113612f}, {0.0163916f, 0.0880132f, 0.8955950f}};
  constexpr float TO_BT709[3][3] = {{1.6604910f, -0.5876411f, -0.0728499f}, {-0.1245505f, 1.1328999f, -0.0083494f}, {-0.0181508f, -0.1005789f, 1.1187297f}};
  std::array<float, 3> wide = {};
  for (int row = 0; row < 3; ++row) {
    wide[row] = std::max(TO_BT2020[row][0] * x[0] + TO_BT2020[row][1] * x[1] + TO_BT2020[row][2] * x[2], 0.f);
  }
  const float peak = ((settings.rendering_mode >= 0.5f) ? settings.peak : std::max(settings.paper_white, settings.ui)) / settings.ui;
  const float largest = std::max({wide[0], wide[1], wide[2]});
  if (largest > peak) {
    for (float& channel : wide) {
      channel *= peak / largest;
    }
  }
  std::array<float, 3> out = {};
  if (settings.encoding == 0) {
    constexpr float M1 = 2610.f / 16384.f;
    constexpr float M2 = 128.f * (2523.f / 4096.f);
    constexpr float C1 = 3424.f / 4096.f;
    constexpr float C2 = 32.f * (2413.f / 4096.f);
    constexpr float C3 = 32.f * (2392.f / 4096.f);
    for (int channel = 0; channel < 3; ++channel) {
      const float y_m1 = std::pow(std::max(wide[channel] * settings.ui / 10000.f, 0.f), M1);
      out[channel] = std::pow((C1 + C2 * y_m1) / (1.f + C3 * y_m1), M2);
    }
    return out;
  }
  for (int row = 0; row < 3; ++row) {
    out[row] = (TO_BT709[row][0] * wide[0] + TO_BT709[row][1] * wide[1] + TO_BT709[row][2] * wide[2]) * settings.ui / 80.f;
  }
  return out;
}

float Half(float value) { return renodx::utils::float16::Decode(renodx::utils::float16::Encode(value)); }

struct Capture {
  uint32_t format = 0;  // DXGI / ReShade format
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t row_pitch = 0;
  std::vector<uint8_t> rows;

  std::array<float, 3> Pixel(uint32_t x, uint32_t y) const {
    const uint8_t* at = rows.data() + (static_cast<size_t>(y) * row_pitch);
    if (format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
      uint16_t halves[4];
      std::memcpy(halves, at + (static_cast<size_t>(x) * 8), 8);
      return {renodx::utils::float16::Decode(halves[0]), renodx::utils::float16::Decode(halves[1]), renodx::utils::float16::Decode(halves[2])};
    }
    uint32_t packed = 0;
    std::memcpy(&packed, at + (static_cast<size_t>(x) * 4), 4);
    if (format == DXGI_FORMAT_R10G10B10A2_UNORM) {
      return {static_cast<float>(packed & 1023u), static_cast<float>((packed >> 10) & 1023u), static_cast<float>((packed >> 20) & 1023u)};
    }
    return {static_cast<float>(packed & 255u), static_cast<float>((packed >> 8) & 255u), static_cast<float>((packed >> 16) & 255u)};
  }
};

std::optional<Capture> ReadCapture(uint32_t present) {
  std::ifstream file(here / std::format("capture_{:05}.raw", present), std::ios::binary);
  if (!file) return std::nullopt;
  char magic[8] = {};
  uint32_t fields[6] = {};
  file.read(magic, 8);
  file.read(reinterpret_cast<char*>(fields), sizeof(fields));
  Capture capture = {.format = fields[0], .width = fields[1], .height = fields[2], .row_pitch = fields[3]};
  capture.rows.resize(static_cast<size_t>(capture.row_pitch) * capture.height);
  file.read(reinterpret_cast<char*>(capture.rows.data()), static_cast<std::streamsize>(capture.rows.size()));
  if (std::string_view(magic) != "RDXCAP1" || !file) return std::nullopt;
  return capture;
}

enum class Expect { UPGRADED, PLAIN, BROKEN, NO_OVERLAY };

struct Frame {
  uint32_t present = 0;      // zz_capture's present number
  uint32_t chain_frame = 0;  // 1: the first after the chain / picture was made
  uint32_t width = 0;
  uint32_t height = 0;
};

// One frame against the reference: frames >= 2 after each creation must match (UPGRADED / NO_OVERLAY / PLAIN);
// BROKEN expects them not to (today's add-on: the copy skipped).
void CheckFrame(const Frame& frame, Expect expect, const ProxySettings& settings, bool* any_broken_frame) {
  const auto capture = ReadCapture(frame.present);
  if (!capture.has_value()) {
    Fail(std::format("present {}: no capture", frame.present));
    return;
  }
  if (capture->width != frame.width || capture->height != frame.height) {
    Fail(std::format("present {}: capture {}x{}, expected {}x{}", frame.present, capture->width, capture->height, frame.width, frame.height));
    return;
  }
  if (frame.chain_frame < 2) return;
  const uint32_t expected_format = [&] {
    if (expect == Expect::PLAIN) return static_cast<uint32_t>(DXGI_FORMAT_R8G8B8A8_UNORM);
    return static_cast<uint32_t>((settings.encoding == 1) ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R10G10B10A2_UNORM);
  }();
  if (capture->format != expected_format) {
    Fail(std::format("present {}: back buffer format {}, expected {}", frame.present, capture->format, expected_format));
    return;
  }
  const auto boxes = OverlayBoxes(frame.width, frame.height);
  size_t mismatches = 0;
  size_t black = 0;
  float worst = 0.f;
  std::string first;
  for (uint32_t y = 0; y < frame.height; ++y) {
    for (uint32_t x = 0; x < frame.width; ++x) {
      const auto box = OverlayAt(boxes, x, y);
      const auto got = capture->Pixel(x, y);
      std::array<float, 3> want = {};
      float tolerance = 1.f;
      if (expect == Expect::PLAIN) {
        if (box.has_value()) {
          const auto pixel = OverlayPixel(*box, x - boxes[*box].x, y - boxes[*box].y);
          want = {static_cast<float>(pixel[0]), static_cast<float>(pixel[1]), static_cast<float>(pixel[2])};
          tolerance = 0.f;
        } else {
          const auto value = PictureValue(x, y, frame.width, frame.height);
          for (int channel = 0; channel < 3; ++channel) {
            want[channel] = std::round(std::clamp(value[channel], 0.f, 1.f) * 255.f);
          }
        }
      } else {
        std::array<float, 3> input = PictureValue(x, y, frame.width, frame.height);
        if (box.has_value() && expect != Expect::NO_OVERLAY) {
          const auto pixel = OverlayPixel(*box, x - boxes[*box].x, y - boxes[*box].y);
          input = {pixel[0] / 255.f, pixel[1] / 255.f, pixel[2] / 255.f};
        }
        for (float& channel : input) {
          channel = Half(channel);
        }
        want = Proxy(input, settings);
        if (settings.encoding != 1) {
          for (float& channel : want) {
            channel = std::round(std::clamp(channel, 0.f, 1.f) * 1023.f);
          }
        }
      }
      float error = 0.f;
      for (int channel = 0; channel < 3; ++channel) {
        float channel_error = std::abs(got[channel] - want[channel]);
        if (expect != Expect::PLAIN && settings.encoding == 1) {
          // 2^-8 relative (a half-ulp flip of the input's half rounding, raised through gamma 2.2: ~2^-8.6), 2^-14 absolute
          // near zero.
          channel_error /= std::max(std::abs(want[channel]) / 256.f, 1.f / 16384.f);
        }
        error = std::max(error, channel_error);
      }
      if (got[0] == 0.f && got[1] == 0.f && got[2] == 0.f) ++black;
      worst = std::max(worst, error);
      if (error > tolerance) {
        if (mismatches == 0) {
          first = std::format("({}, {}) got {:.5f} {:.5f} {:.5f} want {:.5f} {:.5f} {:.5f}", x, y, got[0], got[1], got[2], want[0], want[1], want[2]);
        }
        ++mismatches;
      }
    }
  }
  const size_t pixels = static_cast<size_t>(frame.width) * frame.height;
  std::cout << std::format("present {} ({}x{}, chain frame {}): {} of {} pixels off the reference (worst {:.2f}), {} black{}{}\n", frame.present,
                           frame.width, frame.height, frame.chain_frame, mismatches, pixels, worst, black, mismatches ? ", first " : "", first);
  if (expect == Expect::BROKEN) {
    if (mismatches * 2 > pixels) *any_broken_frame = true;
    return;
  }
  if (mismatches != 0) Fail(std::format("present {}: {} pixels off the reference, first {}", frame.present, mismatches, first));
}

struct D3D11Game {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<IDXGIFactory2> factory;
  ComPtr<IDXGISwapChain1> swap_chain;
  ComPtr<ID3D11RenderTargetView> back_buffer_rtv;
  ComPtr<ID3D11Texture2D> picture;
  ComPtr<ID3D11RenderTargetView> picture_rtv;
  ComPtr<ID3D11Texture2D> depth;
  ComPtr<ID3D11DepthStencilView> depth_view;
  ComPtr<ID3D11VertexShader> vertex_shader;
  ComPtr<ID3D11PixelShader> pixel_shader;
  ComPtr<ID3D11Buffer> size_buffer;
  ComPtr<ID3D11Buffer> slot15;
  HWND window = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;
};

bool use_warp = true;

// WARP through its adapter (driver type unknown): ReShade skips a device made with D3D_DRIVER_TYPE_WARP.
HRESULT CreateD3D11(ID3D11Device** device, ID3D11DeviceContext** context) {
  ComPtr<IDXGIAdapter> adapter;
  if (use_warp) {
    ComPtr<IDXGIFactory4> factory;
    CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
  }
  const D3D_DRIVER_TYPE type = use_warp ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;
  return D3D11CreateDevice(adapter.Get(), type, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device, nullptr, context);
}

// A device made and released at once (the loader's probe, a plugin's test device).
void ShortDevice() {
  ComPtr<ID3D11Device> device;
  if (FAILED(CreateD3D11(&device, nullptr))) {
    Fail("short-lived D3D11CreateDevice failed");
  }
}

void MakeBackBufferView(D3D11Game* game) {
  ComPtr<ID3D11Texture2D> buffer;
  game->swap_chain->GetBuffer(0, IID_PPV_ARGS(&buffer));
  game->device->CreateRenderTargetView(buffer.Get(), nullptr, &game->back_buffer_rtv);
}

void MakeChain(D3D11Game* game) {
  const DXGI_SWAP_CHAIN_DESC1 desc = {
      .Width = game->width,
      .Height = game->height,
      .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
      .SampleDesc = {.Count = 1},
      .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
      .BufferCount = 2,
      .Scaling = DXGI_SCALING_STRETCH,
      .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
      .Flags = 0,
  };
  if (FAILED(game->factory->CreateSwapChainForHwnd(game->device.Get(), game->window, &desc, nullptr, nullptr, &game->swap_chain))) {
    Fail("CreateSwapChainForHwnd failed");
    return;
  }
  game->factory->MakeWindowAssociation(game->window, DXGI_MWA_NO_ALT_ENTER);
  MakeBackBufferView(game);
}

// The picture after the chain (its upgrade needs the back buffer's size): RGBA8, SRV | RT, its depth, its view.
void MakePicture(D3D11Game* game) {
  const D3D11_TEXTURE2D_DESC desc = {
      .Width = game->width,
      .Height = game->height,
      .MipLevels = 1,
      .ArraySize = 1,
      .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
      .SampleDesc = {.Count = 1},
      .Usage = D3D11_USAGE_DEFAULT,
      .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
  };
  game->device->CreateTexture2D(&desc, nullptr, &game->picture);
  D3D11_TEXTURE2D_DESC depth_desc = desc;
  depth_desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
  depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
  game->device->CreateTexture2D(&depth_desc, nullptr, &game->depth);
  const D3D11_DEPTH_STENCIL_VIEW_DESC depth_view_desc = {.Format = DXGI_FORMAT_D24_UNORM_S8_UINT, .ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D};
  game->device->CreateDepthStencilView(game->depth.Get(), &depth_view_desc, &game->depth_view);
  game->device->CreateRenderTargetView(game->picture.Get(), nullptr, &game->picture_rtv);
}

void ReleasePicture(D3D11Game* game) {
  game->picture_rtv.Reset();
  game->depth_view.Reset();
  game->depth.Reset();
  game->picture.Reset();
}

std::vector<uint8_t> OverlayRows(size_t box, const OverlayBox& rect, bool pitch8) {
  std::vector<uint8_t> rows(static_cast<size_t>(rect.width) * rect.height * (pitch8 ? 8 : 4));
  for (uint32_t j = 0; j < rect.height; ++j) {
    for (uint32_t i = 0; i < rect.width; ++i) {
      const auto pixel = OverlayPixel(box, i, j);
      if (pitch8) {
        // C3: rows already in halves (8 bytes a pixel): passed through, never converted.
        const uint16_t halves[4] = {renodx::utils::float16::Encode(pixel[0] / 255.f), renodx::utils::float16::Encode(pixel[1] / 255.f),
                                    renodx::utils::float16::Encode(pixel[2] / 255.f), renodx::utils::float16::Encode(1.f)};
        std::memcpy(rows.data() + ((static_cast<size_t>(j) * rect.width + i) * 8), halves, 8);
      } else {
        std::memcpy(rows.data() + ((static_cast<size_t>(j) * rect.width + i) * 4), pixel.data(), 4);
      }
    }
  }
  return rows;
}

void DrawOverlay(D3D11Game* game, ID3D11Resource* onto, bool pitch8) {
  const auto boxes = OverlayBoxes(game->width, game->height);
  for (size_t box = 0; box < boxes.size(); ++box) {
    const auto& rect = boxes[box];
    const auto rows = OverlayRows(box, rect, pitch8);
    const D3D11_BOX dest = {.left = rect.x, .top = rect.y, .front = 0, .right = rect.x + rect.width, .bottom = rect.y + rect.height, .back = 1};
    game->context->UpdateSubresource(onto, 0, &dest, rows.data(), rect.width * (pitch8 ? 8 : 4), 0);
  }
}

void DrawFrame(D3D11Game* game, bool fixed_form, bool pitch8) {
  PumpMessages();
  const float size[4] = {static_cast<float>(game->width), static_cast<float>(game->height), 0.f, 0.f};
  game->context->UpdateSubresource(game->size_buffer.Get(), 0, nullptr, size, 0, 0);
  game->context->ClearDepthStencilView(game->depth_view.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);
  ID3D11RenderTargetView* targets[] = {game->picture_rtv.Get()};
  game->context->OMSetRenderTargets(1, targets, game->depth_view.Get());
  const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(game->width), .Height = static_cast<float>(game->height), .MaxDepth = 1.f};
  game->context->RSSetViewports(1, &viewport);
  game->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  game->context->VSSetShader(game->vertex_shader.Get(), nullptr, 0);
  game->context->PSSetShader(game->pixel_shader.Get(), nullptr, 0);
  ID3D11Buffer* constants[] = {game->size_buffer.Get()};
  game->context->PSSetConstantBuffers(0, 1, constants);
  game->context->Draw(3, 0);
  game->context->OMSetRenderTargets(0, nullptr, nullptr);

  ComPtr<ID3D11Texture2D> buffer;
  game->swap_chain->GetBuffer(0, IID_PPV_ARGS(&buffer));
  // The fixed loader: the overlay into buffer 0 when it is RGBA8, else into the picture (RGBA8 of the same size) before
  // the copy, else none. 1.3.20: into buffer 0 after the copy.
  ID3D11Resource* before_copy = nullptr;
  ID3D11Resource* after_copy = buffer.Get();
  if (fixed_form) {
    D3D11_TEXTURE2D_DESC buffer_desc = {};
    buffer->GetDesc(&buffer_desc);
    D3D11_TEXTURE2D_DESC picture_desc = {};
    game->picture->GetDesc(&picture_desc);
    if (buffer_desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM) {
      after_copy = nullptr;
      if (picture_desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM && picture_desc.Width == buffer_desc.Width && picture_desc.Height == buffer_desc.Height) {
        before_copy = game->picture.Get();
      } else {
        Fail("fixed form: neither side 8-bit (the picture's desc changed)");
      }
    }
  }
  if (before_copy != nullptr) DrawOverlay(game, before_copy, pitch8);
  game->context->CopyResource(buffer.Get(), game->picture.Get());
  if (after_copy != nullptr) DrawOverlay(game, after_copy, pitch8);
  game->swap_chain->Present(0, 0);
}

int RunD3D11(bool fixed_form, Expect expect, bool pitch8) {
  D3D11Game game = {.width = 1280, .height = 720};
  game.window = MakeWindow(game.width, game.height);
  if (FAILED(CreateD3D11(&game.device, &game.context))) {
    Fail("the game's D3D11CreateDevice failed");
    return 1;
  }
  ComPtr<IDXGIDevice> dxgi_device;
  game.device.As(&dxgi_device);
  ComPtr<IDXGIAdapter> adapter;
  dxgi_device->GetAdapter(&adapter);
  adapter->GetParent(IID_PPV_ARGS(&game.factory));
  MakeChain(&game);

  ComPtr<ID3DBlob> vs_blob;
  ComPtr<ID3DBlob> ps_blob;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompile(PICTURE_SHADER, sizeof(PICTURE_SHADER) - 1, nullptr, nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vs_blob, &errors))
      || FAILED(D3DCompile(PICTURE_SHADER, sizeof(PICTURE_SHADER) - 1, nullptr, nullptr, nullptr, "ps", "ps_5_0", 0, 0, &ps_blob, &errors))) {
    Fail(std::string("shader compile: ") + (errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?"));
    return 1;
  }
  game.device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &game.vertex_shader);
  game.device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &game.pixel_shader);
  const D3D11_BUFFER_DESC size_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  game.device->CreateBuffer(&size_desc, nullptr, &game.size_buffer);
  // The loader's slot-15 zero vertex buffer, bound once at device creation.
  const std::array<uint8_t, 64> zeros = {};
  const D3D11_BUFFER_DESC slot15_desc = {.ByteWidth = 64, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_VERTEX_BUFFER};
  const D3D11_SUBRESOURCE_DATA slot15_data = {.pSysMem = zeros.data()};
  game.device->CreateBuffer(&slot15_desc, &slot15_data, &game.slot15);
  ID3D11Buffer* slot15_buffers[] = {game.slot15.Get()};
  const UINT stride = 16;
  const UINT offset = 0;
  game.context->IASetVertexBuffers(15, 1, slot15_buffers, &stride, &offset);
  MakePicture(&game);

  const ProxySettings settings = ReadProxySettings();
  std::vector<Frame> frames;
  uint32_t present = 0;
  const auto frames_of = [&](uint32_t count) {
    for (uint32_t frame = 1; frame <= count; ++frame) {
      DrawFrame(&game, fixed_form, pitch8);
      frames.push_back({.present = ++present, .chain_frame = frame, .width = game.width, .height = game.height});
    }
  };
  frames_of(5);

  // Mode change, the loader's order: ClearState, releases, Flush, ResizeBuffers on the same window, a new picture.
  game.context->ClearState();
  ReleasePicture(&game);
  game.back_buffer_rtv.Reset();
  game.context->Flush();
  game.width = 1600;
  game.height = 900;
  if (FAILED(game.swap_chain->ResizeBuffers(0, game.width, game.height, DXGI_FORMAT_UNKNOWN, 0))) Fail("ResizeBuffers failed");
  MakeBackBufferView(&game);
  MakePicture(&game);
  frames_of(3);

  // A new window: the old chain released first.
  game.context->ClearState();
  ReleasePicture(&game);
  game.back_buffer_rtv.Reset();
  game.swap_chain.Reset();
  game.context->Flush();
  DestroyWindow(game.window);
  game.width = 1280;
  game.height = 720;
  game.window = MakeWindow(game.width, game.height);
  MakeChain(&game);
  MakePicture(&game);
  frames_of(3);

  game.context->ClearState();
  game.context->Flush();

  bool any_broken_frame = false;
  for (const auto& frame : frames) {
    CheckFrame(frame, expect, settings, &any_broken_frame);
  }
  if (expect == Expect::BROKEN && !any_broken_frame) Fail("expected the frames to miss the reference (today's add-on), they match");
  DestroyWindow(game.window);
  return 0;
}

// C4: the Mesa route's stand-in, a D3D12 device and flip chain. Frames are clears only (no shaders): their captures are
// compared by run.ps1 with a run without the add-on.
int RunD3D12() {
  ComPtr<IDXGIFactory4> factory;
  CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
  ComPtr<IDXGIAdapter> adapter;
  if (use_warp) {
    factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
  }
  ComPtr<ID3D12Device> device;
  if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
    Fail("D3D12CreateDevice failed");
    return 1;
  }
  const D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
  ComPtr<ID3D12CommandQueue> queue;
  device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue));
  const uint32_t width = 1280;
  const uint32_t height = 720;
  HWND window = MakeWindow(width, height);
  const DXGI_SWAP_CHAIN_DESC1 chain_desc = {
      .Width = width,
      .Height = height,
      .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
      .SampleDesc = {.Count = 1},
      .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
      .BufferCount = 2,
      .Scaling = DXGI_SCALING_STRETCH,
      .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
  };
  ComPtr<IDXGISwapChain1> chain1;
  if (FAILED(factory->CreateSwapChainForHwnd(queue.Get(), window, &chain_desc, nullptr, nullptr, &chain1))) {
    Fail("D3D12 CreateSwapChainForHwnd failed");
    return 1;
  }
  ComPtr<IDXGISwapChain3> chain;
  chain1.As(&chain);
  const D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV, .NumDescriptors = 2};
  ComPtr<ID3D12DescriptorHeap> heap;
  device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap));
  const UINT rtv_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  std::array<ComPtr<ID3D12Resource>, 2> buffers;
  for (UINT index = 0; index < 2; ++index) {
    chain->GetBuffer(index, IID_PPV_ARGS(&buffers[index]));
    D3D12_CPU_DESCRIPTOR_HANDLE handle = heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * rtv_size;
    device->CreateRenderTargetView(buffers[index].Get(), nullptr, handle);
  }
  ComPtr<ID3D12CommandAllocator> allocator;
  device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
  ComPtr<ID3D12GraphicsCommandList> list;
  device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list));
  list->Close();
  ComPtr<ID3D12Fence> fence;
  device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  for (uint64_t frame = 1; frame <= 5; ++frame) {
    PumpMessages();
    const UINT index = chain->GetCurrentBackBufferIndex();
    allocator->Reset();
    list->Reset(allocator.Get(), nullptr);
    D3D12_RESOURCE_BARRIER barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                                      .Transition = {.pResource = buffers[index].Get(),
                                                     .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                                     .StateBefore = D3D12_RESOURCE_STATE_PRESENT,
                                                     .StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET}};
    list->ResourceBarrier(1, &barrier);
    D3D12_CPU_DESCRIPTOR_HANDLE handle = heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * rtv_size;
    const float base[4] = {0.2f, 0.4f, 0.6f, 1.f};
    list->ClearRenderTargetView(handle, base, 0, nullptr);
    const float patch[4] = {1.f, 0.5f, 0.f, 1.f};
    const D3D12_RECT rect = {.left = 100, .top = 100, .right = 400, .bottom = 300};
    list->ClearRenderTargetView(handle, patch, 1, &rect);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    list->ResourceBarrier(1, &barrier);
    list->Close();
    ID3D12CommandList* lists[] = {list.Get()};
    queue->ExecuteCommandLists(1, lists);
    chain->Present(0, 0);
    queue->Signal(fence.Get(), frame);
    fence->SetEventOnCompletion(frame, event);
    WaitForSingleObject(event, 10000);
  }
  CloseHandle(event);
  DestroyWindow(window);
  return 0;
}

// C6: Sol's OpenGL fallback's stand-in, a GL context and SwapBuffers (GPU only: WARP has no GL).
int RunOpenGl() {
  using WglCreateContext = HGLRC(WINAPI*)(HDC);
  using WglMakeCurrent = BOOL(WINAPI*)(HDC, HGLRC);
  using WglDeleteContext = BOOL(WINAPI*)(HGLRC);
  using GlClearColor = void(WINAPI*)(float, float, float, float);
  using GlClear = void(WINAPI*)(uint32_t);
  using GlScissor = void(WINAPI*)(int, int, int, int);
  using GlEnable = void(WINAPI*)(uint32_t);
  using GlDisable = void(WINAPI*)(uint32_t);
  HMODULE gl = LoadLibraryW(L"opengl32.dll");
  if (gl == nullptr) {
    Fail("no opengl32.dll");
    return 1;
  }
  const auto create_context = reinterpret_cast<WglCreateContext>(GetProcAddress(gl, "wglCreateContext"));
  const auto make_current = reinterpret_cast<WglMakeCurrent>(GetProcAddress(gl, "wglMakeCurrent"));
  const auto delete_context = reinterpret_cast<WglDeleteContext>(GetProcAddress(gl, "wglDeleteContext"));
  const auto clear_color = reinterpret_cast<GlClearColor>(GetProcAddress(gl, "glClearColor"));
  const auto clear = reinterpret_cast<GlClear>(GetProcAddress(gl, "glClear"));
  const auto scissor = reinterpret_cast<GlScissor>(GetProcAddress(gl, "glScissor"));
  const auto enable = reinterpret_cast<GlEnable>(GetProcAddress(gl, "glEnable"));
  const auto disable = reinterpret_cast<GlDisable>(GetProcAddress(gl, "glDisable"));
  HWND window = MakeWindow(1280, 720);
  HDC dc = GetDC(window);
  const PIXELFORMATDESCRIPTOR format = {.nSize = sizeof(PIXELFORMATDESCRIPTOR),
                                        .nVersion = 1,
                                        .dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
                                        .iPixelType = PFD_TYPE_RGBA,
                                        .cColorBits = 32,
                                        .cDepthBits = 24,
                                        .cStencilBits = 8};
  SetPixelFormat(dc, ChoosePixelFormat(dc, &format), &format);
  HGLRC context = create_context(dc);
  if (context == nullptr || !make_current(dc, context)) {
    Fail("wglCreateContext / wglMakeCurrent failed");
    return 1;
  }
  constexpr uint32_t GL_COLOR_BUFFER_BIT = 0x4000;
  constexpr uint32_t GL_SCISSOR_TEST = 0x0C11;
  for (int frame = 1; frame <= 5; ++frame) {
    PumpMessages();
    clear_color(0.2f, 0.4f, 0.6f, 1.f);
    clear(GL_COLOR_BUFFER_BIT);
    enable(GL_SCISSOR_TEST);
    scissor(100, 100, 300, 200);
    clear_color(1.f, 0.5f, 0.f, 1.f);
    clear(GL_COLOR_BUFFER_BIT);
    disable(GL_SCISSOR_TEST);
    SwapBuffers(dc);
  }
  make_current(nullptr, nullptr);
  delete_context(context);
  ReleaseDC(window, dc);
  DestroyWindow(window);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  wchar_t exe[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  here = fs::path(exe).parent_path();
  std::vector<std::string_view> args(argv + 1, argv + argc);
  const auto has = [&args](std::string_view what) { return std::ranges::find(args, what) != args.end(); };
  use_warp = !has("gpu");
  const std::string_view api = args.empty() ? "d3d11" : args[0];

  // 1. The game's range, as the loader keeps it before the game thread maps the game.
  if (VirtualAlloc(reinterpret_cast<void*>(GAME_BASE), GAME_RESERVE, MEM_RESERVE, PAGE_NOACCESS) != reinterpret_cast<void*>(GAME_BASE)) {
    Fail("0x400000 not reservable");
  }
  AddVectoredExceptionHandler(1, CountReservedRangeViolations);
  // 2. The loader's probe device.
  ShortDevice();
  CheckAddonsFreed("probe device", 1);
  // 3. The loader's handler and filter, then the game mapped.
  AddVectoredExceptionHandler(1, LoaderCrashFilter);
  SetUnhandledExceptionFilter(LoaderUnhandledNote);
  if (!has("image_none")) CommitSyntheticElf(has("image_badmagic"), has("image_table"));
  // 3b. A plugin's test device.
  ShortDevice();
  CheckAddonsFreed("plugin test device", 2);

  int result = 0;
  if (api == "d3d12") {
    result = RunD3D12();
  } else if (api == "opengl") {
    result = RunOpenGl();
  } else {
    const bool fixed_form = (args.size() > 1 && args[1] == "fixed");
    Expect expect = Expect::UPGRADED;
    if (has("plain")) expect = Expect::PLAIN;
    if (has("broken")) expect = Expect::BROKEN;
    if (has("no_overlay")) expect = Expect::NO_OVERLAY;
    result = RunD3D11(fixed_form, expect, has("pitch8"));
  }
  if (reserved_range_violations.load() != 0) Fail(std::format("{} access violations inside 0x400000 + 16 MB", reserved_range_violations.load()));
  const bool passed = (result == 0 && failures.empty());
  std::ofstream(here / "result.txt") << (passed ? "PASS" : "FAIL") << '\n';
  std::cout << (passed ? "PASS" : "FAIL") << '\n';
  return passed ? 0 : 1;
}
