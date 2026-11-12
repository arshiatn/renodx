# Facts that differ per TW game

Answer every row for a new game before planning, with a proof level (A, B, C as in `SKILL.md`). Each one looked portable once and wasn't. "Known answers" are what Attila and WH3 (and others where read) do; they are examples, not defaults.

## Fact sheet

| Fact | Known answers | Read it from |
|---|---|---|
| Bitness and device | Attila, Rome II, Thrones are 32-bit (`build32`). Attila, Rome II, Thrones and Shogun 2 make a single-threaded D3D11 device (Flags 0x1): no call on it from another thread | An unmodded `ReShade.log` |
| The game's MSAA | WH3 and Shogun 2 have none; the other TW games do, and the maintainer plays with it (in-game MSAA silently keeps DLAA off). Every listed hash has an `_ms` permutation reading Texture2DMS: `LIGHT_BOUNCE_DRAWS` once lacked them and the feature did nothing. Rows proven in WH3 were proven without MSAA | The dump's `_ms` names; MSAA on in the data run and the settings matrix |
| Is the exe readable | Troy, WH1, WH2 and WH3 are protected: tweak names and types come from Pharaoh's code (`tw_borrow64.py`, `tw_convert64.py`); WH3's `.arch` section is Denuvo, `.xpdata` plain. No detour on exe code without a test | The exe's sections; `wh3_batch/reports/notdone.md` |
| Swap chain after a device loss | WH3, Pharaoh and Troy keep the old device and make a new chain; Attila destroys the device first | `ReShade.log` around a loss; `device_loss` order |
| Settings register and swap-chain constants | Attila b13, WH3 b10: a HUD-less image and GT-VBAO drawn with b13 in WH3 came out black or flat | The game's `shared.h` |
| Depth direction | Read it per game; it sets the inversion flags for FSR, XeSS and FG | TAA's nearest-depth test in its CS |
| Final encoding | Attila's target is sqrt-encoded; another tone mapper may end in gamma 2.2. SDR output once re-encoded the gamma 2.0 frame as 2.2 and brightened the darks | Last lines of the tone mapper; WARP with a known ramp |
| What the sun shader's shadow term is | Patch routes: `mov_sat` then `div` (Attila, Thrones); `min` with a temp (Rome II); x(1 - t49) after each comparison (Troy, Pharaoh, WH1, WH2); `t_shadow` mask reads (WH3). Insert by the instruction's own length (a fixed +5 tokens once broke Rome II's 7-token `min`); WH3's AO matched the `min` pair until the `t_shadow` reads won [b7076619] | WARP on the game's bytes: t49, t50, t51 unbound equals the game; t49 = 1 equals the game with its mask at 0 |
| Sun shader rules | WH3's 7 sun shaders take the diffuse by 3 rules (metal or not, F0's source) and decode normals 2 ways: `SunDraw::diffuse_rule` | The addon's function against the game's picture in one WARP test |
| Where the ambient comes from | Attila: the cube, `normalize(n x (1, 4, 1))`, a scale switch. WH3: spherical harmonics or the cube x `g_ambient_fudge_factor`, and the light probes *replace* either through a lerp on their fade, bringing the sky's light, its shade and the bounce; then AO and the GI's openness. The same float that is Attila's scale is WH3's `g_ssr_enabled` | Every source that writes the term, in the sun shader's disassembly |
| The GI flag in steady state | WH3: on mid-battle (60 of 61 traces), 0 on a battle's first frame. A one-shot first-frame log reported it off | Count flag values across mid-battle traces, as the game uploaded them (before any override) |
| Camera and depth range | Near and far; sky at z = w (skip it); the campaign backdrop at 75% of far; a side-on sun has w = 0 (z = `_m22 / _m23`, w's sign the divisor's) | A trace's camera constants |
| World scale | The campaign world is 1/20 to 1/100 of a battle's: every metre constant needs a campaign scale (`PlacementScale`). Far over near says nothing: WH3's campaign far is 1/26 of a battle's, its trees about 1/6 | Measure an object in a trace |
| Who owns the render scale | Attila: the addon. WH3: the game's `gfx_resolution_scale`, which rounds sizes up and truncates floats; TAA's c37 (`g_screen_size`) against c39 (`g_viewport_dimensions`), equal at 100%; AO read at uv x `ssao_overscan / warscape_3d_overscan` (1.5 at 50%) | The TAA's b0; t00's overscan (c6.y at b4) |
| Jitter | Attila: the addon injects Halton(2,3). WH3 keeps the game's: 16 phases, 0.43 render px at 100% and 50% | Harness sign pairs; the TAA's constants per frame |
| Shadow maps | WH3: cascade splits 0.1 / 40 / 150 / 350 / 4000; a kept static cache of two arrays that swap on scroll (copy shader C883E3FA); trees drawn each frame only into cascade 1 (40 m), farther ones only at the cache's rare refresh; the hills' shadow is a height-map shadow map, not a cascade | Log the splits; array swaps in a trace; the shadow pass's draws by family |
| The game's own screen-space shadows | Shogun 2's sss00/01 *is* Bend. WH3's shadow mask E6BCBA8A has its own contact march (`gfx_screen_space_shadows`) and smoke (`t_vfx_shadows`). Replace, don't stack | The dump: a march in the mask shader; smoke textures it reads |
| Reflections | WH3's SSR gates on water alone; its branch for the rest is dead code. Water draws its own G-buffer after the deferred light. Lobe: Attila alpha <= 0.10, WH3 roughness squared over 2 [35ccb165] | Asm: the flag test before the march; the trace's order |
| Fog | WH3's fog marches its shadow maps; Attila's has none | Fog PS bytes |
| Light draws | WH3's light VS: POSITION1 the colour, POSITION2 position and reach; 156 lights in a siege. Light colours differ in scale: `Host::local_light_scale` from the light PS's own diffuse literal | The light VS input layout; the light PS |
| G-buffer colour for thin replays | Power 2 in Attila and Thrones, 1 in WH3 | The G-buffer PS's output |
| G-buffer material byte | WH3: channel 3's alpha at `gbuffer_slot + 2`, bit 128 foliage (its sun mask lets light through), 64 water, 16 own motion; Attila: nothing marks foliage | The sun and lighting PS's `and` on that channel; values from a trace's readback |
| Tone mapper | WH2 and WH3 use a per-map Reinhard: the hand-off must match its value and its log-log slope at mid grey. Attila's bloom high pass caps luminance at 25 (a flat halo in HDR: extend it logarithmically); WH3 has no cap. Hue Clip's default from the OkLab hue error against that game's SDR clip: Attila 40, WH3 100 | The tone mapper's bytes; `test/tw_psychov35` |
| Test keys | Plant Wind Sway exists in Attila only; WH3 has no such value | The game's preferences file |

Outside the usual pattern:

- **Shogun 2**: graphics rows live in its `_dlss` addon, which has its own trace header; tweaks are built in `empire.retail.x64.dll` as `lea rdi, [name]; lea rsi, [obj]; call`, the value at obj + 0xb8 for ints, a form `tw_tweaks.py` and `engine_tweaks::Find` don't read.
- **Rome Remastered**: Feral's Forward+, no G-buffer pass (ASSAO, a CSM resolve that is a screen-space shadow mask, Feral SSR compute, SMAA and FXAA): the G-buffer and sun-shader rows here don't apply.

## Traps when reading a fact

| Assumption | What the code really does | Check |
|---|---|---|
| A patch that returns bytes landed where it was meant | `Patch`'s pair search matched WH3's AO instead of its shadow, `PatchAmbient`'s anchor fell inside the spherical-harmonics branch: both logged "patched", nothing failed. Say what the patched register multiplies, per shader | WARP on the game's bytes against pictures made another way (`warp_tests/sun_wh3`): t49 = 1 equals the game with its mask at 0; t51 = 1 leaves 0; game minus patched equals the formula |
| Tweak names exist in today's exe | Lists rot. Lookup needs a NUL-preceded whole string (`twrome2/engine_tweaks.hpp`) | Scan the exe offline for each name, count what resolves |
| A tweak name that resolves is a value | `Find` also lands on plain words passed to other calls, and on a preference's flag byte instead of its value. Writing there corrupts data or faults. List names from the value constructors' own call sites | Replay `Find` offline, check each address against the constructor that built it |
| A factor changes any tweak | A value of 0 stays 0. WH3's shadow splits are 0 and the game then uses a table, so they need `.absolute`. A cap of 0 can mean "no cap": read the reader's early exit before setting a slider's minimum | Read the reader's branch on the value |
| An engine on/off value can be written live | WH3's `gi_enabled` written 0, then 1 mid-battle: the GI stage ran for the first time without textures and crashed [3ae82d0e]. A render stage's value may go on to off live, never off to on, and never be held off while the stage is built. Prefer the shader's constant flag | Read the value's constructor and its graphics-change listener in the exe |
| An at-start test can stay when its names don't exist | `StartAtStart` retries, and the addon's present path waits for it, so a missing name holds every slider back | Grep the list against the replay; a list with no at-start test skips the thread |
| A buffer is known by its size | Attila's 480-byte `lighting_VS_PS` is the only buffer of that size in the dump; WH3's 224-byte GI constants are not (18 and 83 others), so they are taken at their dispatch | Count every buffer of that size in the dump first |
| A mapped buffer's pointer is the frame's | A map's pointer is written only by the thread that mapped it, at its unmap (deferred contexts) | Read per thread |
| Values assumed until seen | WH3 assumed c37 = 1 / target before any run logged it | Name every assumed constant in the commit until a run logs it |
| The decompiler names the right member | It can name a wrong member of the right buffer; `cb_audit` passes and only WARP against the game's bytes shows it. Give that test the game's real ranges (whole cascade count, rising distances) or noise hides it | WH3 mask E6BCBA8A: `movc r0.y, cb3[2].y` printed as the wrong bool |
