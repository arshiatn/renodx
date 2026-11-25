# tw_callisto_diffuse: Callisto diffuse gates

`src/games/twattila/callisto_diffuse.hlsli` (`CallistoLobe`) checked against its CPU twin (`tests.cpp`: the header's
math in float, HLSL's pow as exp2(e log2 x)), and the twin against a double slide model (c1, c2 in its three alpha_s
readings, Proxima; slides 85 / 90 / 128 of Jimenez and Petersen, "The Rendering of The Callisto Protocol", SIGGRAPH
2023 Advances). The twin and the probe include the one `src/games/twattila/callisto_diffuse.h` (the sets and
constants). The header runs in `callisto_probe.hlsl` on WARP twice: fxc `cs_5_0 /O3` on D3D11 and dxc `cs_6_5 -O3` on
D3D12; dxc `cs_6_9` is compiled and checked statically. Design: `plan_1117/designs/callisto_diffuse.md` R1.8 and
R1.10 (Revision 1.1); the fix round after its C1 code review (`callisto_diffuse_c1_review.md`, S1-S4 and NITs).
Nothing calls the lobe yet (C2a / C2b / C3 wire it in).

- `slide_points.h`: the references, independent of `callisto_diffuse.h`, written by `python -I make_points.py
  <testbin/scratch/cal_/callisto_check.txt> <testbin/scratch/cdr_/cdr_callisto.txt> slide_points.h`:
  - the talk's knobs (10 published fits, Proxima at 4 alphas, c2 at o 0.25 / 1 / -1 in 3 readings);
  - 2875 point values of R from make_points.py's own numpy reading (explicit h vectors);
  - 94 integrals recomputed at full precision on `tests.cpp`'s grid;
  - the 153 values the two earlier numeric checks printed (`cdr_callisto.txt`, a Gauss-Legendre reading, and
    `callisto_check.txt`, a midpoint reading: the third and fourth readings of the slides);
  - the design's knob table (`DESIGN_SETS`, R1.8 / R1.10): rho / falloff n / tangent falloff m per set, each DI twin's
    parent, kept or not. A's and the planted set's Fresnel knobs are the talk's 0.75 defaults (unused at rho_f 1);
    A+F's Fresnel is red fabric 2's, read from the talk's fits.
- `CALLISTO_SET_A_F` is not in the header (it is scoring only: rho_f > 1, and the cap acts on it). `tests.cpp` keeps
  it as its one scoring-only set, so the probe sees it as an own (dynamic) set and through `CALLISTO_COST_KNOBS`.
- Run (WARP only, no GPU job): `powershell -File test\tw_callisto_diffuse\run.ps1 [-Bin <folder of dxcompiler.dll and
  dxil.dll>] [-Out <build folder>]`. Builds with clang-cl into `tools\tw\testbin\tw_callisto_diffuse` (or `-Out`), logs
  to `last_run.txt` there (UTF-8), and exits 0 only when every gate passes and every control fails. About 10 s.
- `IncludeBesideParent` and the D3D11 / D3D12 WARP harness (about 190 lines) are copies of `test/tw_vmf_diffuse`'s.
  A third WARP test should move them into a shared test header.

## Why the bars catch slips

- WARP vs twin is the same code, so the bar is 1e-4 x max(|twin|, 1) on every Callisto output of every record. WARP's
  own finite check cannot see a NaN inside the lobe (GPU min returns the other operand); the twin's `std::isfinite`
  (its clamp and min keep NaN) plus the 1e-4 bar is the proof.
- A slip in `callisto_diffuse.h` moves the twin as well. The header's numbers are tied to the design's knob table
  instead: rho and the exponents 10 (1 - knob) within 1e-6, each DI twin's retro equal to its parent's, and
  inverse_bond against the bond of the knob model (not of the header's own exponents) within 1e-6. T-twin's double
  model is the knob model too. T-bond has a known answer (1), and the fade and cap are checked against the design's 0.1
  and 4.
- T-bond fails a kept set the cap acts on (the cap would break brightness-kept unseen); only a set on `tests.cpp`'s
  scoring-only list (A+F) gets an INFO line.
- The fold and the base clamp are belt and braces: either alone keeps 0^0 finite in IEEE. Only the source shows the
  clamp's need (dxc's fast math may not meet log2(0) = -inf; WARP is IEEE). Both compilers fold an exponent-0 factor and
  a rho of 1: DXIL Log count per lobe A 2, A+F 4, V16 3, V40_DI 2.
- The "two exponents swapped" control is a twin with retro_exponent and retro_tangent_exponent swapped, compared with
  the WARP outputs of the real header (what a swapped header would give).

## Results (2026-11-25; twin / fxc WARP / dxc WARP)

| Gate | Bar | Result |
|---|---|---|
| T-slides points (2875) | 1e-9 relative | 4.4e-16 |
| T-slides integrals vs make_points.py (94, same grid) | 1e-6 relative | 4.7e-15 |
| T-slides printed by the earlier checks (153) | 1e-3 max(\|ref\|, 0.1) + half a printed unit | 0.39 of the tolerance |
| design: rho and exponents vs the knob table, 7 sets | 1e-6 | 9.5e-8 |
| design: DI twins' retro = parent's, rho_f 1 | exact | V16_DI, V40_DI |
| bonds (knob model, unfaded, double, 96^3) | inverse_bond x bond within 1e-6; unkept: inverse_bond 1 | 9.5e-8; A 1.0121782, V16 0.9685975, V16_DI 1.0521452, V40 0.8966906, V40_DI 1.0833684, planted 1.7819908 (unkept), A+F 1.1816264 |
| T-twin vs the knob model, u clamp + cap + fade, u >= 1e-4 | 1e-5 relative | 1.9e-6 |
| T-twin raw, u >= 0.1 | 1e-5 relative | 3.7e-7 (103 records above the cap skipped: A+F, planted) |
| T2 reciprocity, u >= 0.1 | 1e-6 relative | 0 (twin, fxc, dxc) |
| T-bond kept sets, fade in; the cap acts on none | \|bond - 1\| <= 1e-3 | 4.95e-4 (V40_DI); A 0.99972, V16 1.00012, V16_DI 0.99974, V40 1.00002 |
| T-bond A+F (scoring only, INFO) | | the cap acts: 0.98903 (unfaded 0.99068, uncapped 1.00000) |
| T3 amount 0; defaults at amount 1 | exactly 1.0f | exact, all three |
| T4 finite; R in [0, 4]; 1 at n.l / n.v <= 0 | | all three, every record (14 own sets incl. the bound sets and A+F x 171 edge records x amounts 0 / 0.5 / 1 / -1) |
| T4 fade (n.v 0.03 / 0.05 / 0.08; the probe sets and A+F as the own set) | 1e-4 | twin 1.2e-7, fxc 2.1e-7, dxc 2.1e-7 |
| T4 either guard alone | finite | finite |
| WARP vs twin | 1e-4 x max(\|twin\|, 1) | fxc 4.4e-7, dxc 4.4e-7 |
| T-vmf, closed grid n.l, n.v 0.2..1 (161) x phi 0..180 (181) | rms <= 2 %; max <= 40 % (V16) / 20 % (V40) | V16 1.53 % / 37.6 % (n.l = n.v = 0.2, phi 149); V40 1.21 % / 16.1 % (phi 15); fxc and dxc |
| static | no warnings; no indexable temp / icb (fxc); no alloca / wave op / half (dxc 6.5, 6.9) | clean |

Cost line (one lobe, the probe's `cost` with the set's knobs as a static const, minus `cost_base`, plus its 2 extra
multiplies):

| Set | A | V16 | V16_DI | V40 | V40_DI | planted | A+F |
|---|---|---|---|---|---|---|---|
| fxc cs_5_0 instruction slots | 40 | 48 | 40 | 48 | 40 | 38 | 54 |
| dxc cs_6_5 DXIL instructions | 42 | 51 | 42 | 51 | 42 | 41 | 56 |

This is above the design's estimate of 25-40 per lobe (review NIT 9, for the design owner).

## Controls (each must fail; all did)

| Control | Gate it fails | Result |
|---|---|---|
| 5n read as n^5 | T-slides points / integrals | 3.7e2 / 3.0e2 relative |
| r(x) = 2x | T-slides points / integrals | 1.2 / 0.77 |
| theta_d and theta_h swapped inside alpha_r | T-slides points / integrals | 2.7 / 0.29 |
| V16's n_r off by 1e-3 in the knob table | design and bonds | knob gap 1.0e-2, bond gap 5.0e-4 |
| the twin without the fade | T-twin clamped | 3.0 |
| cos_h from n.l alone (twin) | T2 | 3.0 |
| c2 at o 0.25, radians (slide model) | T2 | 0.29 |
| inverse_bond x 1.01 | T-bond | min \|bond - 1\| 9.5e-3 |
| rho_r 1 + 2^-20 | T3 bit-exact | R != 1.0f (twin, fxc, dxc) |
| neither the fold nor the base clamp | T4 finite (twin) | both NaN: at h = n (n_r 1) and at l = v (n_f 1) |
| fade at 0.08 | T4 fade | 0.18 |
| retro exponents swapped | WARP vs twin | 4.0 (fxc, dxc) |
| Lambert | T-vmf rms | 15.08 % / 24.26 % (fxc, dxc) |
