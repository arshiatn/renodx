# Divinity: Original Sin Enhanced Edition — RenoDX

Replace the complete `src/games/divinityoriginalsinenhanced` folder in a
compatible RenoDX checkout. Rebuild the addon and all shaders together: the
shared shader-data layout has been cleaned up. Include all `.hlsli` files and
keep one replacement for shader hash `0x30A1D40D`. This archive contains source,
not a compiled DLL.

## Rendering modes

| Mode | Menu name | Behavior |
| --- | --- | --- |
| 0 | SDR (Native) | Native tone curve, color grading and fade. |
| 1 | HDR (UC2 Extended / PsychoV30) | UC2 Extended, game grading recovery and full-color PsychoV30. Default. |
| 2 | HDR (UC2 Extended / Pragmap) | UC2 Extended, peak-dependent Pragmap and tint handling. |

Both presets select **UC2 Extended / PsychoV30**. Existing SDR and Pragmap
selections keep their IDs. A saved PsychoV30 selection using ID 3 maps to ID 1.
Other unsupported IDs fall back to PsychoV30. Internally, `HDR` is 0 for SDR
and 1 for both HDR modes.

## Controls and presets

Only controls used by the selected rendering mode are visible. Effect and
PsychoV30 controls require **Advanced** settings.

| Controls | Available in |
| --- | --- |
| Scene white, UI white, output format, UI visibility | All modes |
| Peak, bloom, vignette, godrays, player/local light intensity | Both HDR modes |
| Exposure, highlights, shadows, contrast, cone response, purity | UC2 Extended / PsychoV30 |
| Candle/particle multiplier; brazier color/brightness toggle | UC2 Extended / PsychoV30 |
| Pragmap fire/candle boosts | UC2 Extended / Pragmap |

In PsychoV30 mode, **Fire / Particles > Brazier Color / Brightness Boost**
applies the original fixed color boost when On. Off uses stock emission.

| Channel | Multiplier before tonemapping |
| --- | ---: |
| Red | 6.0 |
| Green | 1.5 |
| Blue | 1.2 |

**Default** sets this boost Off; **What I use** sets it On. Existing saved
On/Off preferences are retained. The toggle has no separate strength slider.

For comparisons, press **Default** once, then switch rendering modes without
pressing another preset. Keep the other active effect and display settings
consistent. PsychoV30 grading controls use 1.00 as their neutral value.

## Output

Scene white and UI white control their respective brightness levels. Scene
output uses one scene/UI-white ratio before the output stage applies UI
white. Changing UI white therefore cancels out of the scene's white scaling.

SDR mode is presented inside the existing HDR10/scRGB output container. It
does not switch Windows or the swapchain to native SDR. Both HDR modes retain
their tone and grading algorithms within this shared presentation pipeline.
