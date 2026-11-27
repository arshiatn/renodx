# Builds renodx-tw-dlaa64.exe (the Total War DLAA helper, x64; see main.cpp) into build\tw_dlaa_helper (or the folder
# given as the first argument). It is not a CMake target (the repository's targets are addons); ship it next to the addon
# with nvngx_dlss.dll (and nvngx_dlssd.dll for Ray Reconstruction; for frame generation Streamline's sl.interposer.dll,
# sl.common.dll, sl.dlss_g.dll, sl.reflex.dll, sl.pcl.dll and nvngx_dlssg.dll; for DXR 1.2 the Agility SDK's
# D3D12\D3D12Core.dll; for FSR amd_fidelityfx_loader_dx12.dll, amd_fidelityfx_upscaler_dx12.dll and amd_fidelityfx_framegeneration_dx12.dll; for XeSS libxess.dll, libxess_fg.dll and libxell.dll). The ray tracing shaders (rt_*.cs_6_5.hlsl; rt_trace also for shader model 6.9 with opacity
# micromaps) are compiled with bin\dxc.exe (DXC 1.9.2609.5, Microsoft's v1.9.2609 release: its dxc.exe, dxcompiler.dll
# and dxil.dll) into headers in that folder.
$root = Resolve-Path "$PSScriptRoot\..\..\..\.."
$out = if ($args.Count -gt 0) { $args[0] } else { Join-Path $root "build\tw_dlaa_helper" }
New-Item -ItemType Directory -Force $out | Out-Null
Set-Location $out
# Any failed step ends the script with exit 1 (a failed -ErrorAction Stop copy alone would only end its statement).
trap { break }
# The old exe goes first: a failed build (a shader's or the helper's) leaves none and fails the script.
Remove-Item "$out\renodx-tw-dlaa64.exe" -ErrorAction SilentlyContinue
if (Test-Path "$out\renodx-tw-dlaa64.exe") { throw "$out\renodx-tw-dlaa64.exe could not be deleted (still running?)" }
# A shader that fails to compile fails the build (no helper with an older shader header).
function Dxc {
  & "$root\bin\dxc.exe" @args
  if ($LASTEXITCODE -ne 0) { throw "dxc failed (exit $LASTEXITCODE): $($args -join ' ')" }
}
# NVIDIA's RTXDI-Library 3.2 (NVIDIA RTX SDKs License, github.com/NVIDIA-RTX/RTXDI-Library: commit
# b67f61cd0bfa6792acbf5429bb29a68ee5cdd808) for ReSTIR GI's boiling filter and PT DI when it sits unmodified in
# build\downloads\rtxdi\extracted\RTXDI-Library-b67f61cd (a copy of that commit, never committed here; the build never
# fetches it). Without it the helper builds without RTXDI. The line below says which: a player's helper has it.
$rtxdi = Join-Path $root "build\downloads\rtxdi\extracted\RTXDI-Library-b67f61cd"
$rtxdi_found = Test-Path "$rtxdi\Include\Rtxdi\DI\Reservoir.hlsli"
if ($rtxdi_found) { "RTXDI b67f61cd: found ($rtxdi)" } else { "RTXDI b67f61cd: NOT FOUND ($rtxdi): the helper builds without RTXDI" }
# With it Torch Light in Path-Traced GI's T1 and T2 (PT DI step 3, PATH_REGIR: rt_rtxdi_bridge.hlsli's ReGIR) in every
# includer of rt_path.hlsli (their every function body compiled) and rt_lights' twins (TORCH_IRRADIANCE: its torch light at
# each pixel, in builds of their own: in rt_lights it took registers from every rt_lights); its ReGIR
# build rt_regir_presample and its C++ (RTXDI's ReGIR.cpp, RISBufferSegmentAllocator.cpp beside main.cpp; RtxdiUtils.cpp
# for Path-Traced Torch Light's reservoir layout and neighbour offsets). Without it today's builds.
$regir = if ($rtxdi_found) { @('-D', 'PATH_REGIR', '-I', "$rtxdi\Include") } else { @() }
$regir_cpp = if ($rtxdi_found) { @("-imsvc$rtxdi\Include", "$rtxdi\Source\ReGIR.cpp", "$rtxdi\Source\RISBufferSegmentAllocator.cpp", "$rtxdi\Source\RtxdiUtils.cpp") } else { @() }
Dxc -nologo -T cs_6_5 -E main -O3 -Fh "$out\rt_trace.h" -Vn RT_TRACE "$PSScriptRoot\rt_trace.cs_6_5.hlsl"
Dxc -nologo -T cs_6_9 -E main -O3 -D OPACITY_MICROMAPS -Fh "$out\rt_trace_omm.h" -Vn RT_TRACE_OMM "$PSScriptRoot\rt_trace.cs_6_5.hlsl"
Dxc -nologo -T cs_6_5 -E main -O3 -Fh "$out\rt_lights.h" -Vn RT_LIGHTS "$PSScriptRoot\rt_lights.cs_6_5.hlsl"
Dxc -nologo -T cs_6_9 -E main -O3 -D OPACITY_MICROMAPS -Fh "$out\rt_lights_omm.h" -Vn RT_LIGHTS_OMM "$PSScriptRoot\rt_lights.cs_6_5.hlsl"
Dxc -nologo -T cs_6_5 -E main -O3 -Fh "$out\rt_fog.h" -Vn RT_FOG "$PSScriptRoot\rt_fog.cs_6_5.hlsl"
Dxc -nologo -T cs_6_9 -E main -O3 -D OPACITY_MICROMAPS -Fh "$out\rt_fog_omm.h" -Vn RT_FOG_OMM "$PSScriptRoot\rt_fog.cs_6_5.hlsl"
Dxc -nologo -T cs_6_5 -E main -O3 -Fh "$out\rt_bounce.h" -Vn RT_BOUNCE "$PSScriptRoot\rt_bounce.cs_6_5.hlsl"
Dxc -nologo -T cs_6_9 -E main -O3 -D OPACITY_MICROMAPS -Fh "$out\rt_bounce_omm.h" -Vn RT_BOUNCE_OMM "$PSScriptRoot\rt_bounce.cs_6_5.hlsl"
Dxc -nologo -T cs_6_5 -E main -O3 @regir -Fh "$out\rt_path.h" -Vn RT_PATH "$PSScriptRoot\rt_path.cs_6_5.hlsl"
Dxc -nologo -T cs_6_9 -E main -O3 @regir -D OPACITY_MICROMAPS -Fh "$out\rt_path_omm.h" -Vn RT_PATH_OMM "$PSScriptRoot\rt_path.cs_6_5.hlsl"
Dxc -nologo -T cs_6_5 -E main -O3 @regir -D REFLECT -Fh "$out\rt_reflect.h" -Vn RT_REFLECT "$PSScriptRoot\rt_path.cs_6_5.hlsl"
Dxc -nologo -T cs_6_9 -E main -O3 @regir -D REFLECT -D OPACITY_MICROMAPS -Fh "$out\rt_reflect_omm.h" -Vn RT_REFLECT_OMM "$PSScriptRoot\rt_path.cs_6_5.hlsl"
Dxc -nologo -T cs_6_5 -E main -O3 @regir -D REUSE -Fh "$out\rt_reuse.h" -Vn RT_REUSE "$PSScriptRoot\rt_path.cs_6_5.hlsl"
Dxc -nologo -T cs_6_9 -E main -O3 @regir -D REUSE -D OPACITY_MICROMAPS -Fh "$out\rt_reuse_omm.h" -Vn RT_REUSE_OMM "$PSScriptRoot\rt_path.cs_6_5.hlsl"
Dxc -nologo -T cs_6_5 -E main -O3 @regir -D REFLECT_RESERVOIR -Fh "$out\rt_reflect_reservoir.h" -Vn RT_REFLECT_RESERVOIR "$PSScriptRoot\rt_path.cs_6_5.hlsl"
Dxc -nologo -T cs_6_9 -E main -O3 @regir -D REFLECT_RESERVOIR -D OPACITY_MICROMAPS -Fh "$out\rt_reflect_reservoir_omm.h" -Vn RT_REFLECT_RESERVOIR_OMM "$PSScriptRoot\rt_path.cs_6_5.hlsl"
# Builds used only with their switches (the builds above stay as without them): Torch and Fire Light in the Light Bounce
# (TORCHES_IN_BOUNCE: the reuse with a local light's sample a bounce texel, the local lights' trace that leaves them to it)
# Lean Reuse (REUSE_LEAN: the reuse's neighbours with fewer fetches), Near Reuse (REUSE_NEAR: fewer, nearer neighbours),
# the reflections of one sample a pixel (REFLECT_ONE_SAMPLE) and the paths of one ray a texel (PATH_ONE_SAMPLE): the same
# picture in fewer registers; Balanced ReSTIR Light's reuse (REUSE_BALANCED: G1 Basic; REUSE_BALANCED_RAYTRACED: G1
# Raytraced) as each of the four reuse builds.
# RENODX_TW_REFERENCE_BUILD set: ReSTIR GI's test switches' too (REUSE_TESTS: the paths and the
# reuse with PT DI step 2's suspects and bias correction, rt_path.hlsli REUSE_SUSPECTS; tests only, a player's helper has none).
$variants = @(@('rt_path', 'TORCHES_IN_BOUNCE REUSE', 'rt_reuse_lights', 'RT_REUSE_LIGHTS'), @('rt_lights', 'TORCHES_IN_BOUNCE', 'rt_lights_bounce', 'RT_LIGHTS_BOUNCE'),
              @('rt_path', 'REUSE_LEAN REUSE', 'rt_reuse_lean', 'RT_REUSE_LEAN'), @('rt_path', 'REUSE_NEAR REUSE', 'rt_reuse_near', 'RT_REUSE_NEAR'),
              @('rt_path', 'REUSE_BALANCED REUSE', 'rt_reuse_balanced', 'RT_REUSE_BALANCED'),
              @('rt_path', 'REUSE_BALANCED TORCHES_IN_BOUNCE REUSE', 'rt_reuse_lights_balanced', 'RT_REUSE_LIGHTS_BALANCED'),
              @('rt_path', 'REUSE_BALANCED REUSE_LEAN REUSE', 'rt_reuse_lean_balanced', 'RT_REUSE_LEAN_BALANCED'),
              @('rt_path', 'REUSE_BALANCED REUSE_NEAR REUSE', 'rt_reuse_near_balanced', 'RT_REUSE_NEAR_BALANCED'),
              @('rt_path', 'REUSE_BALANCED_RAYTRACED REUSE', 'rt_reuse_balanced_raytraced', 'RT_REUSE_BALANCED_RAYTRACED'),
              @('rt_path', 'REUSE_BALANCED_RAYTRACED TORCHES_IN_BOUNCE REUSE', 'rt_reuse_lights_balanced_raytraced', 'RT_REUSE_LIGHTS_BALANCED_RAYTRACED'),
              @('rt_path', 'REUSE_BALANCED_RAYTRACED REUSE_LEAN REUSE', 'rt_reuse_lean_balanced_raytraced', 'RT_REUSE_LEAN_BALANCED_RAYTRACED'),
              @('rt_path', 'REUSE_BALANCED_RAYTRACED REUSE_NEAR REUSE', 'rt_reuse_near_balanced_raytraced', 'RT_REUSE_NEAR_BALANCED_RAYTRACED'),
              @('rt_path', 'REFLECT_ONE_SAMPLE REFLECT', 'rt_reflect_one', 'RT_REFLECT_ONE'), @('rt_path', 'PATH_ONE_SAMPLE', 'rt_path_one', 'RT_PATH_ONE'))
Remove-Item "$out\rt_path_tests.h", "$out\rt_path_tests_omm.h", "$out\rt_reuse_tests.h", "$out\rt_reuse_tests_omm.h" -ErrorAction SilentlyContinue
if ($env:RENODX_TW_REFERENCE_BUILD) {
  $variants += @(@('rt_path', 'REUSE_TESTS', 'rt_path_tests', 'RT_PATH_TESTS'), @('rt_path', 'REUSE_TESTS REUSE', 'rt_reuse_tests', 'RT_REUSE_TESTS'))
}
foreach ($variant in $variants) {
  $defines = $variant[1].Split(' ') | ForEach-Object { '-D', $_ }
  $path_regir = if ($variant[0] -eq 'rt_path') { $regir } else { @() }  # rt_lights_bounce today's (its twin below)
  Dxc -nologo -T cs_6_5 -E main -O3 @defines @path_regir -Fh "$out\$($variant[2]).h" -Vn $variant[3] "$PSScriptRoot\$($variant[0]).cs_6_5.hlsl"
  Dxc -nologo -T cs_6_9 -E main -O3 @defines @path_regir -D OPACITY_MICROMAPS -Fh "$out\$($variant[2])_omm.h" -Vn "$($variant[3])_OMM" "$PSScriptRoot\$($variant[0]).cs_6_5.hlsl"
}
# RENODX_TW_PATH_PROBE_BUILD set: the paths' probe build too (rt_path.hlsli ProbeTexel, tests only; a player's helper has none).
Remove-Item "$out\rt_path_probe.h", "$out\rt_path_probe_omm.h" -ErrorAction SilentlyContinue
if ($env:RENODX_TW_PATH_PROBE_BUILD) {
  Dxc -nologo -T cs_6_5 -E main -O3 @regir -D PATH_PROBE -Fh "$out\rt_path_probe.h" -Vn RT_PATH_PROBE "$PSScriptRoot\rt_path.cs_6_5.hlsl"
  Dxc -nologo -T cs_6_9 -E main -O3 @regir -D PATH_PROBE -D OPACITY_MICROMAPS -Fh "$out\rt_path_probe_omm.h" -Vn RT_PATH_PROBE_OMM "$PSScriptRoot\rt_path.cs_6_5.hlsl"
}
# RENODX_TW_REFERENCE_BUILD set: the reference build's passes too (rt_reference.hlsli, tests only; a player's helper has none).
$reference_passes = @('accumulate', 'census', 'digest', 'group', 'paths', 'reflect', 'state')
foreach ($pass in $reference_passes) { Remove-Item "$out\rt_reference_$pass.h" -ErrorAction SilentlyContinue }
if ($env:RENODX_TW_REFERENCE_BUILD) {
  foreach ($pass in $reference_passes) {
    Dxc -nologo -T cs_6_5 -E main -O3 @regir -D "REF_$($pass.ToUpper())" -Fh "$out\rt_reference_$pass.h" -Vn "RT_REFERENCE_$($pass.ToUpper())" "$PSScriptRoot\rt_reference.cs_6_5.hlsl"
  }
}
# RENODX_TW_RECORDER_BUILD set: the bridge recorder and its player too (rt_d3d12.hpp RENODX_RECORDER, tests only; a released helper has none).
Remove-Item "$out\rt_recorder_build.h", "$out\rt_burn.h", "$out\rt_moments.h" -ErrorAction SilentlyContinue
$recorder_include = @()
if ($env:RENODX_TW_RECORDER_BUILD) {
  Set-Content -Path "$out\rt_recorder_build.h" -Value '#pragma once' -Encoding ascii
  # The resident's (rt_serve.h): its burn and moments passes, nlohmann's JSON for its control files.
  Dxc -nologo -T cs_6_5 -E main -O3 -Fh "$out\rt_burn.h" -Vn RT_BURN "$PSScriptRoot\rt_burn.hlsl"
  Dxc -nologo -T cs_6_5 -E main -O3 -Fh "$out\rt_moments.h" -Vn RT_MOMENTS "$PSScriptRoot\rt_moments.hlsl"
  $recorder_include = @("-imsvc$root\external\json\single_include")
}
Dxc -nologo -T cs_6_0 -E main -O3 -Fh "$out\fg_ui.h" -Vn FG_UI "$PSScriptRoot\fg_ui.cs_6_0.hlsl"
# Path Tracing SER's paths built as the compute ones: the general build and the one of one ray a texel.
Dxc -nologo -T lib_6_9 -O3 @regir -D OPACITY_MICROMAPS -D PATH_REORDER -Fh "$out\rt_path_ser.h" -Vn RT_PATH_SER "$PSScriptRoot\rt_path_ser.lib_6_9.hlsl"
Dxc -nologo -T lib_6_9 -O3 @regir -D OPACITY_MICROMAPS -D PATH_REORDER -D PATH_ONE_SAMPLE -Fh "$out\rt_path_ser_one.h" -Vn RT_PATH_SER_ONE "$PSScriptRoot\rt_path_ser.lib_6_9.hlsl"
# NVIDIA's SHaRC 1.8.3 (NVIDIA RTX SDKs License, github.com/NVIDIA-RTX/SHARC: its untagged main commit 4e21b585 "v1.8.3";
# 1.6.5 was a release) for SHaRC Radiance Cache: fetched into build\downloads\sharc\extracted\NVIDIA-RTX-SHARC-4e21b585 when
# missing, unmodified with NVIDIA's licence (never committed here), its headers compiled into the paths' SHaRC variants
# (for one ray a texel too, PATH_ONE_SAMPLE), its update and resolve. Without it the helper builds without SHaRC; a SHaRC
# shader that fails to compile fails the build. Older folders there (1.6.5's NVIDIA-RTX-SHARC-0b9f58b) stay for older trees.
$sharc_commit = '4e21b585c33c83d723ca9a1e11bbb1090d145793'
$sharc = Join-Path $root "build\downloads\sharc\extracted\NVIDIA-RTX-SHARC-$($sharc_commit.Substring(0, 8))"
if (-not (Test-Path "$sharc\include\SharcCommon.h")) {
  $sharc_zip = Join-Path $root "build\downloads\sharc\SHARC-$sharc_commit.zip"
  try {
    New-Item -ItemType Directory -Force (Join-Path $root "build\downloads\sharc\extracted") | Out-Null
    Invoke-WebRequest -UseBasicParsing "https://github.com/NVIDIA-RTX/SHARC/archive/$sharc_commit.zip" -OutFile $sharc_zip
    Expand-Archive $sharc_zip (Join-Path $root "build\downloads\sharc\extracted") -Force
    Rename-Item (Join-Path $root "build\downloads\sharc\extracted\SHARC-$sharc_commit") $sharc
  } catch {
    "SHaRC 1.8.3 could not be fetched ($_): the helper builds without SHaRC"
  }
}
Remove-Item "$out\rt_path_sharc.h", "$out\rt_path_sharc_one.h", "$out\rt_sharc_update.h", "$out\rt_sharc_resolve.h" -ErrorAction SilentlyContinue
if (Test-Path "$sharc\include\SharcCommon.h") {
  $sharc_version = (Select-String -Path "$sharc\include\SharcCommon.h" -Pattern '#define SHARC_VERSION_(MAJOR|MINOR|BUILD)\s+(\d+)' |
                    ForEach-Object { $_.Matches[0].Groups[2].Value }) -join '.'
  if ($sharc_version -ne '1.8.3') { throw "SHaRC in $sharc is $sharc_version, not 1.8.3" }
  $sharc_flags = @('-nologo', '-T', 'cs_6_9', '-E', 'main', '-O3', '-D', 'OPACITY_MICROMAPS', '-D', 'SHARC', '-enable-16bit-types', '-I', "$sharc\include")
  foreach ($sharc_shader in @(@('rt_path.cs_6_5', 'SHARC_QUERY=1', 'rt_path_sharc', 'RT_PATH_SHARC'), @('rt_path.cs_6_5', 'SHARC_QUERY=1 PATH_ONE_SAMPLE', 'rt_path_sharc_one', 'RT_PATH_SHARC_ONE'),
                              @('rt_sharc_update', 'SHARC_UPDATE=1 SHARC_PROPAGATION_DEPTH=2', 'rt_sharc_update', 'RT_SHARC_UPDATE'), @('rt_sharc_resolve', '', 'rt_sharc_resolve', 'RT_SHARC_RESOLVE'))) {
    $defines = $sharc_shader[1].Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries) | ForEach-Object { '-D', $_ }
    $sharc_regir = if ($sharc_shader[2] -eq 'rt_sharc_update') { @() } else { $regir }  # rt_sharc_update today's (its twin below)
    Dxc @sharc_flags @defines @sharc_regir -Fh "$out\$($sharc_shader[2]).h" -Vn $sharc_shader[3] "$PSScriptRoot\$($sharc_shader[0]).hlsl"
  }
}
# RTXDI's builds (old outputs removed first): ReSTIR GI's boiling filter (REUSE_BOIL, rt_path.hlsli BoilTexel) and
# Torch Light in Path-Traced GI's ReGIR build (rt_regir_presample), rt_lights' twins with its T1 (TORCH_IRRADIANCE) and
# rt_sharc_update's with its T2 (PATH_REGIR: in rt_sharc_update it took local memory from every SHaRC update).
Remove-Item "$out\rt_reuse_boil.h", "$out\rt_regir_presample.h", "$out\rt_lights_torch*.h", "$out\rt_lights_bounce_torch*.h", "$out\rt_sharc_update_regir.h" -ErrorAction SilentlyContinue
if ($rtxdi_found) {
  Dxc -nologo -T cs_6_5 -E main -O3 @regir -D REUSE_BOIL -Fh "$out\rt_reuse_boil.h" -Vn RT_REUSE_BOIL "$PSScriptRoot\rt_path.cs_6_5.hlsl"
  Dxc -nologo -T cs_6_5 -E main -O3 @regir -Fh "$out\rt_regir_presample.h" -Vn RT_REGIR_PRESAMPLE "$PSScriptRoot\rt_regir_presample.hlsl"
  Dxc -nologo -T cs_6_5 -E main -O3 @regir -D TORCH_IRRADIANCE -Fh "$out\rt_lights_torch.h" -Vn RT_LIGHTS_TORCH "$PSScriptRoot\rt_lights.cs_6_5.hlsl"
  Dxc -nologo -T cs_6_9 -E main -O3 @regir -D TORCH_IRRADIANCE -D OPACITY_MICROMAPS -Fh "$out\rt_lights_torch_omm.h" -Vn RT_LIGHTS_TORCH_OMM "$PSScriptRoot\rt_lights.cs_6_5.hlsl"
  Dxc -nologo -T cs_6_5 -E main -O3 @regir -D TORCH_IRRADIANCE -D TORCHES_IN_BOUNCE -Fh "$out\rt_lights_bounce_torch.h" -Vn RT_LIGHTS_BOUNCE_TORCH "$PSScriptRoot\rt_lights.cs_6_5.hlsl"
  Dxc -nologo -T cs_6_9 -E main -O3 @regir -D TORCH_IRRADIANCE -D TORCHES_IN_BOUNCE -D OPACITY_MICROMAPS -Fh "$out\rt_lights_bounce_torch_omm.h" -Vn RT_LIGHTS_BOUNCE_TORCH_OMM "$PSScriptRoot\rt_lights.cs_6_5.hlsl"
  if ($sharc_flags) {
    Dxc @sharc_flags -D SHARC_UPDATE=1 -D SHARC_PROPAGATION_DEPTH=2 @regir -Fh "$out\rt_sharc_update_regir.h" -Vn RT_SHARC_UPDATE_REGIR "$PSScriptRoot\rt_sharc_update.hlsl"
  }
}
# Matched ReSTIR Neighbours' reuse (REUSE_COMPATIBLE, PT DI step 5's G5: RTXDI's compatibility score; its PT header's
# uncalled selection pass gets its surface read and buffer name from rt_path.hlsli) and its Balanced ReSTIR Light twins.
$compatible = @(@('REUSE_COMPATIBLE REUSE', 'rt_reuse_compatible', 'RT_REUSE_COMPATIBLE'),
                @('REUSE_BALANCED REUSE_COMPATIBLE REUSE', 'rt_reuse_compatible_balanced', 'RT_REUSE_COMPATIBLE_BALANCED'),
                @('REUSE_BALANCED_RAYTRACED REUSE_COMPATIBLE REUSE', 'rt_reuse_compatible_balanced_raytraced', 'RT_REUSE_COMPATIBLE_BALANCED_RAYTRACED'))
foreach ($variant in $compatible) {
  Remove-Item "$out\$($variant[1]).h", "$out\$($variant[1])_omm.h" -ErrorAction SilentlyContinue
  if (-not $rtxdi_found) { continue }
  $defines = $variant[0].Split(' ') | ForEach-Object { '-D', $_ }
  Dxc -nologo -T cs_6_5 -E main -O3 @defines -I "$rtxdi\Include" -Fh "$out\$($variant[1]).h" -Vn $variant[2] "$PSScriptRoot\rt_path.cs_6_5.hlsl"
  Dxc -nologo -T cs_6_9 -E main -O3 @defines -D OPACITY_MICROMAPS -I "$rtxdi\Include" -Fh "$out\$($variant[1])_omm.h" -Vn "$($variant[2])_OMM" "$PSScriptRoot\rt_path.cs_6_5.hlsl"
}
# Path-Traced Torch Light (PT DI step 4c): rt_lights' LIGHTS_DI build (the tiles' members, the DI pixels' words), its
# twin with T1 (rt_lights_di_torch), both with Torch and Fire Shadows from the Light Bounce (rt_lights_di_bounce,
# rt_lights_di_bounce_torch), and the DI passes (PATH_DI: rt_di_initial, rt_di_temporal, rt_di_spatial, rt_di_shade),
# each with its OMM twin. The passes' files are plain .hlsl: the add-on's CMake glob builds every *.cs_6_5.hlsl here,
# without RTXDI's include path (rt_path_ser.lib_6_9.hlsl's way).
$di_builds = @(@('rt_lights.cs_6_5', 'LIGHTS_DI', 'rt_lights_di', 'RT_LIGHTS_DI'), @('rt_lights.cs_6_5', 'LIGHTS_DI TORCH_IRRADIANCE', 'rt_lights_di_torch', 'RT_LIGHTS_DI_TORCH'),
               @('rt_lights.cs_6_5', 'LIGHTS_DI TORCHES_IN_BOUNCE', 'rt_lights_di_bounce', 'RT_LIGHTS_DI_BOUNCE'),
               @('rt_lights.cs_6_5', 'LIGHTS_DI TORCHES_IN_BOUNCE TORCH_IRRADIANCE', 'rt_lights_di_bounce_torch', 'RT_LIGHTS_DI_BOUNCE_TORCH'),
               @('rt_di_initial', 'PATH_DI', 'rt_di_initial', 'RT_DI_INITIAL'),
               @('rt_di_temporal', 'PATH_DI', 'rt_di_temporal', 'RT_DI_TEMPORAL'), @('rt_di_spatial', 'PATH_DI', 'rt_di_spatial', 'RT_DI_SPATIAL'),
               @('rt_di_shade', 'PATH_DI', 'rt_di_shade', 'RT_DI_SHADE'))
foreach ($di in $di_builds) { Remove-Item "$out\$($di[2]).h", "$out\$($di[2])_omm.h" -ErrorAction SilentlyContinue }
if ($rtxdi_found) {
  foreach ($di in $di_builds) {
    $defines = $di[1].Split(' ') | ForEach-Object { '-D', $_ }
    Dxc -nologo -T cs_6_5 -E main -O3 @regir @defines -Fh "$out\$($di[2]).h" -Vn $di[3] "$PSScriptRoot\$($di[0]).hlsl"
    Dxc -nologo -T cs_6_9 -E main -O3 @regir @defines -D OPACITY_MICROMAPS -Fh "$out\$($di[2])_omm.h" -Vn "$($di[3])_OMM" "$PSScriptRoot\$($di[0]).hlsl"
  }
}
# The reference build's RTXDI GI contest (RENODX_TW_REUSE_RTXDI; rt_path.hlsli RtxdiPackTexel, ReuseTexel with REUSE_RTXDI).
Remove-Item "$out\rt_reuse_rtxdi_pack.h", "$out\rt_reuse_rtxdi.h", "$out\rt_reuse_rtxdi_omm.h" -ErrorAction SilentlyContinue
if ($rtxdi_found -and $env:RENODX_TW_REFERENCE_BUILD) {
  Dxc -nologo -T cs_6_5 -E main -O3 -D REUSE_RTXDI_PACK -I "$rtxdi\Include" -Fh "$out\rt_reuse_rtxdi_pack.h" -Vn RT_REUSE_RTXDI_PACK "$PSScriptRoot\rt_path.cs_6_5.hlsl"
  Dxc -nologo -T cs_6_5 -E main -O3 -D REUSE -D REUSE_TESTS -D REUSE_RTXDI -I "$rtxdi\Include" -Fh "$out\rt_reuse_rtxdi.h" -Vn RT_REUSE_RTXDI "$PSScriptRoot\rt_path.cs_6_5.hlsl"
  Dxc -nologo -T cs_6_9 -E main -O3 -D REUSE -D REUSE_TESTS -D REUSE_RTXDI -D OPACITY_MICROMAPS -I "$rtxdi\Include" -Fh "$out\rt_reuse_rtxdi_omm.h" -Vn RT_REUSE_RTXDI_OMM "$PSScriptRoot\rt_path.cs_6_5.hlsl"
}
foreach ($shader in @(@('rt_soft', 'RT_SOFT'), @('rt_scene_data', 'RT_SCENE_DATA'), @('rt_compact', 'RT_COMPACT'), @('rt_place', 'RT_PLACE'),
                      @('rt_omm_uvs', 'RT_OMM_UVS'), @('rt_sway', 'RT_SWAY'), @('rt_probe', 'RT_PROBE'))) {
  Dxc -nologo -T cs_6_5 -E main -O3 -Fh "$out\$($shader[0]).h" -Vn $shader[1] "$PSScriptRoot\$($shader[0]).cs_6_5.hlsl"
}
foreach ($shader in @(@('rt_cache_fill', 'RT_CACHE_FILL'), @('rt_cache_resolve', 'RT_CACHE_RESOLVE'), @('rt_reflect_filter', 'RT_REFLECT_FILTER'))) {
  Dxc -nologo -T cs_6_5 -E main -O3 @regir -Fh "$out\$($shader[0]).h" -Vn $shader[1] "$PSScriptRoot\$($shader[0]).cs_6_5.hlsl"
}
# NVIDIA's NVAPI (MIT, github.com/NVIDIA/nvapi) for RTX Mega Geometry when it sits in build\downloads\nvapi (else the
# helper builds without it).
$nvapi = Join-Path $root "build\downloads\nvapi\extracted\nvapi-main"
$nvapi_include = @()
$nvapi_library = @()
if (Test-Path "$nvapi\nvapi.h") {
  $nvapi_include = @("-imsvc$nvapi")
  $nvapi_library = @("$nvapi\amd64\nvapi64.lib")
}
# NVIDIA's Opacity Micro-Map SDK 1.12.0 (NVIDIA RTX SDKs License, github.com/NVIDIA-RTX/Omm: its untagged main commit
# b485825a, built from source as Release with OMM_ENABLE_PRECOMPILED_SHADERS_DXIL on and SPIR-V off; 1.12 has the CPU
# Wrap fix for negative UVs on non-power-of-two textures) for the leaves' micromaps when it sits in
# build\downloads\omm\extracted (bin\omm-lib.dll, include\omm.h); its omm-lib.dll (loaded at run time) ships next to the
# helper.
$omm = Join-Path $root "build\downloads\omm\extracted"
$omm_include = @()
if (Test-Path "$omm\include\omm.h") {
  $omm_include = @("-imsvc$omm\include")
  Copy-Item "$omm\bin\omm-lib.dll" $out -Force -ErrorAction Stop
}
# AMD's FSR SDK (github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK: tag v2.4.0) when it sits in build\downloads\fsr\extracted:
# loaded at run time, its DLLs next to the helper (else none: the helper builds without it). Its Kits\FidelityFX\docs\license.md
# names the files under MIT (its notice ships with them), among them the signedbin DLLs used here and the api, upscalers and
# framegeneration include headers; the files it does not name are under AMD's binary-only licence (no reverse engineering).
$fsr = Join-Path $root "build\downloads\fsr\extracted\FidelityFX-SDK-2.4.0\Kits\FidelityFX"
$fsr_include = @()
if (Test-Path "$fsr\api\include\ffx_api.h") {
  $fsr_include = @("-imsvc$fsr\api\include", "-imsvc$fsr\upscalers\include", "-imsvc$fsr\framegeneration\include")
  Copy-Item "$fsr\signedbin\amd_fidelityfx_loader_dx12.dll", "$fsr\signedbin\amd_fidelityfx_upscaler_dx12.dll", "$fsr\signedbin\amd_fidelityfx_framegeneration_dx12.dll" $out -Force -ErrorAction Stop
}
# Intel's XeSS SDK (Intel Simplified Software License, github.com/intel/xess: its 3.0.2 release) when it sits in
# build\downloads\xess\extracted: loaded at run time, its libxess.dll, libxess_fg.dll and libxell.dll next to the helper.
$xess = Join-Path $root "build\downloads\xess\extracted"
$xess_include = @()
if (Test-Path "$xess\inc\xess\xess_d3d12.h") {
  $xess_include = @("-imsvc$xess\inc")
  Copy-Item "$xess\bin\libxess.dll", "$xess\bin\libxess_fg.dll", "$xess\bin\libxell.dll" $out -Force -ErrorAction Stop
}
# NVIDIA's NRD (NVIDIA RTX SDKs License, github.com/NVIDIA-RTX/NRD: tag v4.18.0, source only) for the Denoiser setting when it
# sits built in build\downloads\nrd\NRD: git clone --recursive --branch v4.18.0, then cmake -S . -B _Build -G Ninja
# -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_FLAGS=-mssse3
# -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DNRD_STATIC_LIBRARY=ON -DNRD_EMBEDS_SPIRV_SHADERS=OFF -DNRD_EMBEDS_DXBC_SHADERS=OFF
# -DSHADERMAKE_FXC_PATH=<fxc.exe>, cmake --build _Build --target NRD. Its library goes into the helper (else the helper
# builds without it).
$nrd = Join-Path $root "build\downloads\nrd\NRD"
$nrd_include = @()
$nrd_library = @()
$nrd_signals = @('SUN', 'AMBIENT', 'BOUNCE', 'REFLECT')
foreach ($signal in $nrd_signals) { Remove-Item "$out\rt_nrd_front_$signal.h", "$out\rt_nrd_finish_$signal.h" -ErrorAction SilentlyContinue }
if (Test-Path "$nrd\_Bin\NRD.lib") {
  $nrd_include = @("-imsvc$nrd\Include")
  $nrd_library = @("$nrd\_Bin\NRD.lib", "$nrd\_Build\_deps\shadermake-build\ShaderMakeBlob.lib")
  foreach ($signal in $nrd_signals) {
    Dxc -nologo -T cs_6_5 -E main -O3 -I "$nrd\Shaders" -D "NRD_FRONT_$signal" -Fh "$out\rt_nrd_front_$signal.h" -Vn "RT_NRD_FRONT_$signal" "$PSScriptRoot\rt_nrd_front.hlsl"
    Dxc -nologo -T cs_6_5 -E main -O3 -I "$nrd\Shaders" -D "NRD_FINISH_$signal" -Fh "$out\rt_nrd_finish_$signal.h" -Vn "RT_NRD_FINISH_$signal" "$PSScriptRoot\rt_nrd_finish.hlsl"
  }
}
# NVIDIA's Nsight Graphics SDK headers (Apache 2.0, include\nsight_graphics_sdk): main.cpp's frame boundaries for Nsight
# Graphics, nothing without it.
& "C:\Program Files\LLVM\bin\clang-cl.exe" /nologo -std:c++20 /O2 /MT /EHsc /DWIN32 /D_WINDOWS -DNOMINMAX `
  -D_ITERATOR_DEBUG_LEVEL=0 "-imsvc$root\include" "-imsvc$root\external\reshade" "-imsvc$root\external\DLSS\include" `
  "-imsvc$root\external\Streamline\include" @nvapi_include @omm_include @fsr_include @xess_include @nrd_include @recorder_include "/I$out" `
  "$PSScriptRoot\main.cpp" @regir_cpp /Ferenodx-tw-dlaa64.exe /link /SUBSYSTEM:WINDOWS `
  "$root\external\DLSS\lib\Windows_x86_64\x64\nvsdk_ngx_s.lib" @nvapi_library @nrd_library d3d11.lib d3d12.lib dxgi.lib shell32.lib version.lib user32.lib advapi32.lib wintrust.lib crypt32.lib dbghelp.lib `
  delayimp.lib /DELAYLOAD:dxgi.dll /DELAYLOAD:d3d11.dll /DELAYLOAD:d3d12.dll
if ($LASTEXITCODE -ne 0) { throw "renodx-tw-dlaa64.exe failed to build (clang-cl exit $LASTEXITCODE)" }
