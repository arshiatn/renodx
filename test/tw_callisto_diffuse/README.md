# tw_callisto_diffuse: Callisto diffuse C1 gates

`src/games/twattila/callisto_diffuse.hlsli` (`CallistoLobe`) checked against its CPU twin (`tests.cpp`: the header's
math in float, HLSL's pow as exp2(e log2 x)), and the twin against a double slide model (c1, c2 in its three alpha_s
readings, Proxima; slides 85 / 90 / 128 of Jimenez and Petersen, "The Rendering of The Callisto Protocol", SIGGRAPH
2023 Advances). The twin and the probe include the one `src/games/twattila/callisto_diffuse.h` (the sets and
constants). The header runs in `callisto_probe.hlsl` on WARP twice: fxc `cs_5_0 /O3` on D3D11 and dxc `cs_6_5 -O3` on
D3D12; dxc `cs_6_9` is compiled and checked statically. Design: `plan_1117/designs/callisto_diffuse.md` R1.8 and
R1.10 (Revision 1.1). Nothing calls the lobe yet (C2a / C2b / C3 wire it in).

- `slide_points.h`: the references, independent of `callisto_diffuse.h`, written by `python -I make_points.py
  <testbin/scratch/cal_/callisto_check.txt> <testbin/scratch/cdr_/cdr_callisto.txt> slide_points.h`:
  - the talk's knobs (10 published fits, Proxima at 4 alphas, c2 at o 0.25 / 1 / -1 in 3 readings);
  - 2875 point values of R from make_points.py's own numpy reading (explicit h vectors);
  - 94 integrals recomputed at full precision on `tests.cpp`'s grid;
  - the 153 values cal_ and cdr_ printed. `cdr_/cdr_callisto.txt` (cdr_'s Gauss-Legendre reading) and
    `cal_/callisto_check.txt` (cal_'s midpoint reading) are the third and fourth readings of the slides.
- Run (WARP only, no GPU job): `powershell -File test\tw_callisto_diffuse\run.ps1 [-Bin <folder of dxcompiler.dll and
  dxil.dll>] [-Out <build folder>]`. Builds with clang-cl into `tools\tw\testbin\tw_callisto_diffuse` (or `-Out`), logs
  to `last_run.txt` there, and exits 0 only when every gate passes and every control fails. About 10 s.
- `IncludeBesideParent` is a copy of `test/tw_vmf_diffuse`'s (fxc's nested include rule); test files share no header.

## Why the bars catch slips

- WARP vs twin is the same code, so the bar is 1e-4 x max(|twin|, 1) on every Callisto output of every record. WARP's
  own finite check cannot see a NaN inside the lobe (GPU min returns the other operand); the twin's `std::isfinite`
  (its clamp and min keep NaN) plus the 1e-4 bar is the proof.
- A slip in `callisto_diffuse.h` moves the twin as well, so the header's numbers have their own references: the bonds
  against the double model (1e-6), T-bond's known answer (1), the fade and cap against the design's 0.1 and 4.
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
| T-slides printed cal_ / cdr_ (153) | 1e-3 max(\|ref\|, 0.1) + half a printed unit | 0.39 of the tolerance |
| bonds (unfaded, double, 96^3) | inverse_bond x bond within 1e-6 | 9.6e-8; A 1.0121782, A+F 1.1816264, V16 0.9685975, V16_DI 1.0521452, V40 0.8966906, V40_DI 1.0833684, planted 1.7819908 (unkept) |
| T-twin, u clamp + cap + fade, u >= 1e-4 | 1e-5 relative | 1.9e-6 |
| T-twin raw, u >= 0.1 | 1e-5 relative | 3.7e-7 (103 records above the cap skipped: A+F, planted) |
| T2 reciprocity, u >= 0.1 | 1e-6 relative | 0 (twin, fxc, dxc) |
| T-bond kept sets, fade in | \|bond - 1\| <= 1e-3 | 4.95e-4 (V40_DI); A 0.99972, V16 1.00012, V16_DI 0.99974, V40 1.00002 |
| T-bond A+F (INFO) | | the cap acts: 0.98903 (unfaded 0.99068, uncapped 1.00000) |
| T3 amount 0; defaults at amount 1 | exactly 1.0f | exact, all three |
| T4 finite; R in [0, 4]; 1 at n.l / n.v <= 0 | | all three, every record (14 own sets incl. the bound sets x 171 edge records x amounts 0 / 0.5 / 1 / -1) |
| T4 fade (n.v 0.03 / 0.05 / 0.08) | 1e-4 | 1.2e-7 / 1.6e-7 / 2.1e-7 |
| T4 either guard alone | finite | finite |
| WARP vs twin | 1e-4 x max(\|twin\|, 1) | fxc 4.4e-7, dxc 4.4e-7 |
| T-vmf, closed grid n.l, n.v 0.2..1 (161) x phi 0..180 (181) | rms <= 2 %; max <= 40 % (V16) / 20 % (V40) | V16 1.53 % / 37.6 % (n.l = n.v = 0.2, phi 149); V40 1.21 % / 16.1 % (phi 15); fxc and dxc |
| static | no warnings; no indexable temp / icb (fxc); no alloca / wave op / half (dxc 6.5, 6.9) | clean |

Cost line (one lobe, the probe's `cost` minus `cost_base` plus its 2 extra multiplies):

| Set | A | A+F | V16 | V16_DI | V40 | V40_DI |
|---|---|---|---|---|---|---|
| fxc cs_5_0 instruction slots | 40 | 54 | 48 | 40 | 48 | 40 |
| dxc cs_6_5 DXIL instructions | 42 | 56 | 51 | 42 | 51 | 42 |

## Controls (each must fail; all did)

| Control | Gate it fails | Result |
|---|---|---|
| 5n read as n^5 | T-slides points / integrals | 3.7e2 / 3.0e2 relative |
| r(x) = 2x | T-slides points / integrals | 1.2 / 0.77 |
| theta_d and theta_h swapped inside alpha_r | T-slides points / integrals | 2.7 / 0.29 |
| cos_h from n.l alone (twin) | T2 | 3.0 |
| c2 at o 0.25, radians (slide model) | T2 | 0.29 |
| inverse_bond x 1.01 | T-bond | min \|bond - 1\| 9.5e-3 |
| rho_r 1 + 2^-20 | T3 bit-exact | R != 1.0f (twin, fxc, dxc) |
| neither the fold nor the base clamp | T4 finite (twin) | NaN at h = n (n_r 1) and at l = v (n_f 1) |
| fade at 0.08 | T4 fade | 0.18 |
| retro exponents swapped | WARP vs twin | 4.0 (fxc, dxc) |
| Lambert | T-vmf rms | 15.08 % / 24.26 % (fxc, dxc) |
