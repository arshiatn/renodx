# R.U.S.E. RenoDX — Direct colour and presentation update

For the 64-bit DX9 game. Keep Windows HDR and the game's **HDR** option on.
This package contains source, not a compiled addon.

## What changed

- PsychoV30 Direct now decodes RGB channel values before Psycho, and derives
  its gray anchor in that same decoded representation. This restores the
  native decoded input colour ratios without an automatic cone multiplier.
  The previous combination of raw RGB and an anchor-only correction could
  look washed out. The visual result needs an in-game comparison.
- A RUSE-only presentation guard suppresses the native DX9 Present after a
  successful DX11 HDR proxy presentation. ReShade's native DX9 runtime runs
  after the proxy; presenting both to the same window can expose the stale
  native buffer and overlay trails. The guard records the actual DXGI result,
  transfers the native VSync interval to the proxy, and falls back to native
  presentation if the proxy fails, is occluded or has no shared image.
  Hooks are removed on swapchain reset/destruction and addon detach. No shared
  RenoDX framework files are changed. This is a workaround for the observed
  corruption, with runtime confirmation still required.
- UI brightness is now applied by 12 identified UI shaders at their final
  backbuffer draws. Scene output and the HDR proxy use a fixed 203-nit reference.
  The scene no longer divides by UI brightness, so a delayed proxy frame cannot
  produce a scene-brightness pulse from mismatched UI values.
- UI draws to offscreen textures and world buffers retain their native output.
  The same shader can appear at several points in the frame; only a tracked
  backbuffer draw gets the new UI gain.
- Native UI RGB/alpha are bounded before brightness scaling. This prevents
  those shaders' out-of-range output reaching the floating-point blend target.
  Their original texture, colour and coverage calculations are retained.
- The failing broad 10-bit-to-FP16 scene upgrade and Scene Precision setting
  are removed. An old saved `RuseScenePrecision` value is ignored. The game keeps
  its native scene/bloom targets; the HDR output intermediate remains FP16.
- Source and destination GPU handoff waits are enabled for the DX9/DX11 proxy.
  They can reduce shared-texture copy races and frame rate. These waits alone
  did not fix the reported loading corruption; they remain enabled while
  testing the presentation guard.
- Settings are captured once per host frame, including known UI-only screens.
  The nested DX11 presentation does not relatch them.

## Rendering modes

| Mode | Behaviour |
|---|---|
| SDR | Native postprocess response and clipping, with Paper White scaling. |
| HDR (Extended) | Native colour processing, gamma and brightness; maps range above SDR white into Peak. |
| HDR (PsychoV30) | Native colour processing and gamma/brightness, then decode and PsychoV30. |
| HDR (PsychoV30 Direct) | Native colour processing and channel decode, then PsychoV30 replaces the native luminance gamma/brightness response. |

**Direct retains the game's grading:** blur, bloom, desaturation, colour tint,
dominant colour and vignette. Both Psycho modes share their user controls and
use the same unchanged PsychoV30 implementation. Their insertion points differ.
Direct derives a decoded neutral input anchor from native Gamma and Brightness.
For native neutral input `x`, the stock decoded output is
`V(x) = (Brightness * x^Gamma)^2.2`. Direct solves `V(x) = 0.18`, then uses
`x^2.2` as its input anchor because its RGB is also decoded with power 2.2.
For positive colours the native gamma/brightness stage is a common RGB gain;
decoding after that gain changes intensity but not decoded chromaticity. Thus
both Psycho insertion points now have the same input colour ratios while their
tonal responses can differ. Neither mode uses a pre-grading LUT bridge.
Both use the Cone Response Exponent slider directly: 1.00 passes exactly 1.00.
Direct is experimental: this preserves the native display-derived colour
representation, and does not prove the earlier texture is physically
scene-linear or reproduce the complete native tone response.

## Findings from the supplied battlefield dump

| Shaders | Role indicated by the code | Action |
|---|---|---|
| `0x232EB5DF` | Final scene blur/bloom, grading and gamma/brightness | Keep the existing HDR replacement; remove its UI divisor. |
| `0x48D24786`, `0xD43CA37B`, `0xA2BBD356`, `0x50BBDEEF`, `0x808A3141`, `0x075A442C`, `0xB2812F27` | HUD/textured/procedural interface draws shown after the scene shader | Add guarded UI scaling. |
| `0xF543EFAC`, `0xC7A516D7` | Additional interface/solid-colour variants | Scale only their backbuffer draws. |
| `0x33688E4E`, `0x5C455E4D`, `0xEB420492` | Scaleform colour/texture variants | Add guarded UI scaling; test menu/loading coverage. |
| `0x3F5A412D`, `0x14E10CD4`, `0xA3E8FBAB` | Interface variants undoing native scene grading before later postprocessing | Leave unchanged. |
| `0xAA46BD6F`, `0xEEDB7573` | Vertical/horizontal filters reading PreviousSceneMap | Leave unchanged; their formats/copy chain are not established. |
| Other shaders | Scene lighting, water simulation, shadows, depth/blur and effects | No speculative replacements or resource upgrades. |

The dump contains 70 files; the relevant postprocess/interface bodies were
inspected. Shader names/math and the captured order identify useful roles,
but do not prove texture formats, copy/resolve aliases,
blend states or the loading screen's complete draw sequence. UI replacements
preserve the decompiled declarations and computations, with `$` removed from
identifier names for HLSL compatibility. Native CSOs remain the reference if
a replacement has a compilation or constant/sampler binding discrepancy.

## Build and check

Replace the existing `src/games/ruse` source folder with this one, rebuild, and
restart the game. Remove the old addon binary before installing the new one;
keep only one RUSE game addon active.

```bat
cmake --preset clang-x64
cmake --build --preset clang-x64-debug --target ruse
```

Expected addon: `renodx-ruse.addon64`. Inspect the generated embeds for
`0x232EB5DF`, the 12 UI hashes above, and both DX11 proxy shaders.

Completed checks: the actual addon body and current RenoDX vtable helper were
compiled against a platform/API shim. Checks cover scene/UI snapshots, settings
and ABI, native/Ex/swapchain presentation, VSync transfer, TEST calls, foreign
devices, proxy failures, occlusion, reset, device loss, installation failure and
teardown. Math checks cover the actual gray calibration and fallback, decoded
colour ratios, unchanged native grading/Psycho code and final output gamut/peak.
These are source and CPU checks. Windows FXC/SDK, the game, GPU copies, FPS and
PS3 instruction limits were unavailable here.

In game, vary UI from low to high while watching uncovered scenery; vary Paper
White while watching opaque HUD elements. Check all four modes, world labels,
menu/loading screens, text edges, fades and the cursor. Compare loading with
the ReShade overlay closed and open, and test without DevKit too. Menus or UI
shaders absent from the supplied dump may need additional coverage.

If loading corruption persists, send `ReShade.log`, the RenoDX commit/branch,
ReShade version, and a loading-frame draw/resource capture. Cursor trails and
incomplete ReShade overlay widgets cannot be diagnosed conclusively from the
game's battlefield pixel shaders. The supplied ReShade 6.8 log shows both game
addon and DevKit loaded, two startup proxy configurations, and a same-window
flip-swapchain warning, but no reported copy/Present failure. First test with
only the rebuilt RUSE addon active, then compare with DevKit enabled.
The current main framework lacks the Saboteur fork's `proxy_skip_host_present`
setting; this package uses the existing vtable helper inside the game addon.
Do not combine this guard with another native Present-suppression addon.

DXVK remains an optional later comparison. Test this update on the currently
working renderer first so its effect is clear.
