# Divinity: Original Sin Enhanced Edition — RenoDX

HDR for the D3D11 game (`EoCApp.exe`, 64-bit). Build target `divinityoriginalsinenhanced` (amd64): rebuild the add-on
and every shader together, the shader-data layout (`shared.h`) is shared by all of them.

## Rendering modes

| Value | Menu name | What it does |
| --- | --- | --- |
| 0 | SDR (Native) | The game's own curve, grade and fade. |
| 1 | HDR (UC2 Extended / PsychoV30) | The earlier mode: UC2 Extended with Musa's PsychoV30 (`twattila/psycho_test30.hlsli`, one copy with Divinity 2 and Attila). |
| 2 | HDR (UC2 Extended / Pragmap) | The earlier mode with Pragmap's fire / candle boosts. |
| 3 | HDR (Extended) | Default and Reset. The game's curve extended past white, then the game's grade once after it (`GradeAfterCurve`: a colour past white graded at its brightest channel's 1 and scaled back; Smooth Grade Shoulder (Experimental, off): a smooth shoulder from 0.8 instead, up to 0.033 OkLab near white), Hue Clip Emulation 70. |
| 4 | HDR (PsychoV35) | Recommended. Musa's PsychoV35 on the game's mid gray, then the grade. |

## Presets

All four keep Peak, Paper White, UI, Settings Mode and Output (SDR also sets Output SDR).

- **Recommended**: HDR (PsychoV35) with less bloom.
- **Reset**: every image row at its default, HDR (Extended).
- **SDR (monitors without HDR)**: SDR (Native) and Output SDR (restart if it was HDR).
- **PsychoV30 (earlier Recommended)**: HDR (UC2 Extended / PsychoV30) with less bloom and the brazier / candle boosts.

## Rows

- General: Peak, Paper White, UI, Rendering Mode. Page 1 says to keep the game's Gamma Correction at 1: no shader of
  the game reads it (its value goes to a renderer method as 1 / value; the tone map encodes a fixed 1 / 2.2).
- Extended: Hue Clip Emulation. PsychoV30 / Pragmap rows only in their modes (Advanced).
- Extra: Bloom, Vignette, Godrays. Game-Specific: UI, Player Light Intensity (lights with shadows: point and spot
  shadow passes), Local Light Intensity (lights without), Sun Shadow Softness (Experimental: the PCSS light size x
  the row, Ultra shadows), Smooth Grade Shoulder (Experimental, off; Extended and PsychoV35).
- Output (restart): Auto (default: scRGB with Windows HDR on, else SDR), HDR10, scRGB, SDR. Decided again at every
  new swap chain (`twattila/output_encoding.hpp`).
- Tools (Advanced): Trace One Frame (`tw_frame_trace.txt`), Time Each Pass (One Frame) (`divinity_pass_timing.txt`).

The game makes a new swap chain 2-4 times a start and at each window-mode change: `twattila/device_loss.hpp` lets the
last flip chain go first (`release_flip_chains_before_recreate`) and handles a removed device. LightVolume's SMAA
passes and the scene's float targets are upgraded with view cloning and the hot swap.

## Tests (offline, WARP)

- `test/tw_divinity1/run.ps1`: the tone map 0x30A1D40D against the game's bytes: SDR (Native) exact, Extended and
  PsychoV35 on the game's mid gray (control: mode 1's curve on a curve), Smooth Grade Shoulder on (slope, the stock
  below 0.8) with a hard-knee control, the row off bit-equal to the tone map before it (a6762afab^; control: the row on).
- `test/tw_d1_post/run.ps1`: 22 replaced passes Vanilla exact against the game's census bytes, each with a wrong copy
  that must fail; the light rows at 2 give twice the game's light.
- `test/tw_d1_swapchain`, `test/tw_device_loss`, `test/tw_output_encoding`: the window's chains, device loss, Output.
- `test/tw_psychov30_nan/run.ps1`: PsychoV30's NaN / infinity guards live against a copy without them.

The census (`tools/nontw/divinity1/census`, the game's shaders from its .pak files and exe) stays outside the repo.

## Engine facts (static reads of EoCApp.exe)

The Steam exe, 21,605,888 bytes, SHA-256 f7c759e7...33d88, ASLR on (addresses at its image base 0x140000000).

- Screen-space reflections: `SSREnabled` in graphicSettings.lsx does nothing. The key is built as a FixedString
  (0x1407989C2), but the lsx loader (0x1407E2D40...: MotionBlurEnabled into the settings object's byte 0x4C,
  SSAOEnabled 0x4D, ShadowsEnabled 0x4F) and the saver (0x1407E2141...: the same keys) skip it, so the lsx the game
  writes at exit should lose the line (a run keeping that lsx confirms it). The settings object (pointer at 0x1412E3E30, built at 0x1407E34C0 / 0x14085E96E: dword
  0x01000101 at 0x4C) keeps byte 0x4E, SSR, at 0. The renderer reads that byte at its start (0x1408225F8) and in its
  settings apply (0x140820990, vtable slot 0x141064570). On a change it creates SSR (0x140820040) or releases it
  (0x14081FEB0). So SSR can only be turned on by writing that byte in the game's memory.

## Credits

RenoDX, PsychoV30, PsychoV35 and Hue Clip Emulation by ShortFuse. This mod uses Musa's custom PsychoV30 and
PsychoV35. UC2 Extended by Musa and DabVeed. Divinity: Original Sin Enhanced Edition by ATN.
