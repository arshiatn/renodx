# Builds renodx-tw-dlaa64.exe (the Total War DLAA helper, x64; see main.cpp) into build\tw_dlaa_helper (or the folder
# given as the first argument). It is not a CMake target (the repository's targets are addons); ship it next to the addon
# with nvngx_dlss.dll (and nvngx_dlssd.dll for Ray Reconstruction; for frame generation Streamline's sl.interposer.dll,
# sl.common.dll, sl.dlss_g.dll, sl.reflex.dll, sl.pcl.dll and nvngx_dlssg.dll; for DXR 1.2 the Agility SDK's
# D3D12\D3D12Core.dll; for FSR amd_fidelityfx_loader_dx12.dll, amd_fidelityfx_upscaler_dx12.dll and amd_fidelityfx_framegeneration_dx12.dll; for XeSS libxess.dll, libxess_fg.dll and libxell.dll). The ray tracing shaders (rt_*.cs_6_5.hlsl; rt_trace also for shader model 6.9 with opacity
# micromaps) are compiled with bin\dxc.exe into headers in that folder.
$root = Resolve-Path "$PSScriptRoot\..\..\..\.."
$out = if ($args.Count -gt 0) { $args[0] } else { Join-Path $root "build\tw_dlaa_helper" }
New-Item -ItemType Directory -Force $out | Out-Null
Set-Location $out
& "$root\bin\dxc.exe" -nologo -T cs_6_5 -E main -O3 -Fh "$out\rt_trace.h" -Vn RT_TRACE "$PSScriptRoot\rt_trace.cs_6_5.hlsl"
& "$root\bin\dxc.exe" -nologo -T cs_6_9 -E main -O3 -D OPACITY_MICROMAPS -Fh "$out\rt_trace_omm.h" -Vn RT_TRACE_OMM "$PSScriptRoot\rt_trace.cs_6_5.hlsl"
& "$root\bin\dxc.exe" -nologo -T cs_6_5 -E main -O3 -Fh "$out\rt_lights.h" -Vn RT_LIGHTS "$PSScriptRoot\rt_lights.cs_6_5.hlsl"
& "$root\bin\dxc.exe" -nologo -T cs_6_9 -E main -O3 -D OPACITY_MICROMAPS -Fh "$out\rt_lights_omm.h" -Vn RT_LIGHTS_OMM "$PSScriptRoot\rt_lights.cs_6_5.hlsl"
& "$root\bin\dxc.exe" -nologo -T cs_6_5 -E main -O3 -Fh "$out\rt_fog.h" -Vn RT_FOG "$PSScriptRoot\rt_fog.cs_6_5.hlsl"
& "$root\bin\dxc.exe" -nologo -T cs_6_9 -E main -O3 -D OPACITY_MICROMAPS -Fh "$out\rt_fog_omm.h" -Vn RT_FOG_OMM "$PSScriptRoot\rt_fog.cs_6_5.hlsl"
& "$root\bin\dxc.exe" -nologo -T cs_6_5 -E main -O3 -Fh "$out\rt_bounce.h" -Vn RT_BOUNCE "$PSScriptRoot\rt_bounce.cs_6_5.hlsl"
& "$root\bin\dxc.exe" -nologo -T cs_6_9 -E main -O3 -D OPACITY_MICROMAPS -Fh "$out\rt_bounce_omm.h" -Vn RT_BOUNCE_OMM "$PSScriptRoot\rt_bounce.cs_6_5.hlsl"
& "$root\bin\dxc.exe" -nologo -T cs_6_5 -E main -O3 -Fh "$out\rt_path.h" -Vn RT_PATH "$PSScriptRoot\rt_path.cs_6_5.hlsl"
& "$root\bin\dxc.exe" -nologo -T cs_6_9 -E main -O3 -D OPACITY_MICROMAPS -Fh "$out\rt_path_omm.h" -Vn RT_PATH_OMM "$PSScriptRoot\rt_path.cs_6_5.hlsl"
& "$root\bin\dxc.exe" -nologo -T cs_6_5 -E main -O3 -D REFLECT -Fh "$out\rt_reflect.h" -Vn RT_REFLECT "$PSScriptRoot\rt_path.cs_6_5.hlsl"
& "$root\bin\dxc.exe" -nologo -T cs_6_9 -E main -O3 -D REFLECT -D OPACITY_MICROMAPS -Fh "$out\rt_reflect_omm.h" -Vn RT_REFLECT_OMM "$PSScriptRoot\rt_path.cs_6_5.hlsl"
& "$root\bin\dxc.exe" -nologo -T cs_6_5 -E main -O3 -D REUSE -Fh "$out\rt_reuse.h" -Vn RT_REUSE "$PSScriptRoot\rt_path.cs_6_5.hlsl"
& "$root\bin\dxc.exe" -nologo -T cs_6_9 -E main -O3 -D REUSE -D OPACITY_MICROMAPS -Fh "$out\rt_reuse_omm.h" -Vn RT_REUSE_OMM "$PSScriptRoot\rt_path.cs_6_5.hlsl"
& "$root\bin\dxc.exe" -nologo -T cs_6_5 -E main -O3 -D REFLECT_RESERVOIR -Fh "$out\rt_reflect_reservoir.h" -Vn RT_REFLECT_RESERVOIR "$PSScriptRoot\rt_path.cs_6_5.hlsl"
& "$root\bin\dxc.exe" -nologo -T cs_6_9 -E main -O3 -D REFLECT_RESERVOIR -D OPACITY_MICROMAPS -Fh "$out\rt_reflect_reservoir_omm.h" -Vn RT_REFLECT_RESERVOIR_OMM "$PSScriptRoot\rt_path.cs_6_5.hlsl"
# Builds used only with their switches (the builds above stay as without them): Torch and Fire Light in the Light Bounce
# (TORCHES_IN_BOUNCE: the reuse with a local light's sample a bounce texel, the local lights' trace that leaves them to it)
# and Lean Reuse (REUSE_LEAN: the reuse's neighbours with fewer fetches).
foreach ($variant in @(@('rt_path', 'TORCHES_IN_BOUNCE REUSE', 'rt_reuse_lights', 'RT_REUSE_LIGHTS'), @('rt_lights', 'TORCHES_IN_BOUNCE', 'rt_lights_bounce', 'RT_LIGHTS_BOUNCE'),
                       @('rt_path', 'REUSE_LEAN REUSE', 'rt_reuse_lean', 'RT_REUSE_LEAN'))) {
  $defines = $variant[1].Split(' ') | ForEach-Object { '-D', $_ }
  & "$root\bin\dxc.exe" -nologo -T cs_6_5 -E main -O3 @defines -Fh "$out\$($variant[2]).h" -Vn $variant[3] "$PSScriptRoot\$($variant[0]).cs_6_5.hlsl"
  & "$root\bin\dxc.exe" -nologo -T cs_6_9 -E main -O3 @defines -D OPACITY_MICROMAPS -Fh "$out\$($variant[2])_omm.h" -Vn "$($variant[3])_OMM" "$PSScriptRoot\$($variant[0]).cs_6_5.hlsl"
}
& "$root\bin\dxc.exe" -nologo -T cs_6_0 -E main -O3 -Fh "$out\fg_ui.h" -Vn FG_UI "$PSScriptRoot\fg_ui.cs_6_0.hlsl"
& "$root\bin\dxc.exe" -nologo -T lib_6_9 -O3 -D OPACITY_MICROMAPS -D PATH_REORDER -Fh "$out\rt_path_ser.h" -Vn RT_PATH_SER "$PSScriptRoot\rt_path_ser.lib_6_9.hlsl"
# NVIDIA's SHaRC (NVIDIA RTX SDKs License, github.com/NVIDIA-RTX/SHARC: its 1.6.5 release) for SHaRC Radiance Cache when
# it sits in build\downloads\sharc\extracted: its headers compiled into the paths' SHaRC variants (else none: the
# helper builds without it).
$sharc = Get-ChildItem (Join-Path $root "build\downloads\sharc\extracted") -Directory -ErrorAction SilentlyContinue |
  Where-Object { Test-Path "$($_.FullName)\include\SharcCommon.h" } | Select-Object -First 1
Remove-Item "$out\rt_path_sharc.h", "$out\rt_sharc_update.h", "$out\rt_sharc_resolve.h" -ErrorAction SilentlyContinue
if ($sharc) {
  $sharc_flags = @('-nologo', '-T', 'cs_6_9', '-E', 'main', '-O3', '-D', 'OPACITY_MICROMAPS', '-D', 'SHARC', '-enable-16bit-types', '-I', "$($sharc.FullName)\include")
  & "$root\bin\dxc.exe" @sharc_flags -Fh "$out\rt_path_sharc.h" -Vn RT_PATH_SHARC "$PSScriptRoot\rt_path.cs_6_5.hlsl"
  & "$root\bin\dxc.exe" @sharc_flags -D SHARC_UPDATE=1 -D SHARC_PROPAGATION_DEPTH=2 -Fh "$out\rt_sharc_update.h" -Vn RT_SHARC_UPDATE "$PSScriptRoot\rt_sharc_update.hlsl"
  & "$root\bin\dxc.exe" @sharc_flags -Fh "$out\rt_sharc_resolve.h" -Vn RT_SHARC_RESOLVE "$PSScriptRoot\rt_sharc_resolve.hlsl"
}
foreach ($shader in @(@('rt_soft', 'RT_SOFT'), @('rt_scene_data', 'RT_SCENE_DATA'), @('rt_compact', 'RT_COMPACT'), @('rt_place', 'RT_PLACE'),
                      @('rt_cache_fill', 'RT_CACHE_FILL'), @('rt_cache_resolve', 'RT_CACHE_RESOLVE'), @('rt_reflect_filter', 'RT_REFLECT_FILTER'),
                      @('rt_omm_uvs', 'RT_OMM_UVS'), @('rt_sway', 'RT_SWAY'))) {
  & "$root\bin\dxc.exe" -nologo -T cs_6_5 -E main -O3 -Fh "$out\$($shader[0]).h" -Vn $shader[1] "$PSScriptRoot\$($shader[0]).cs_6_5.hlsl"
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
# NVIDIA's Opacity Micro-Map SDK (NVIDIA RTX SDKs License, github.com/NVIDIA-RTX/OMM: its Windows release) for the
# leaves' micromaps when it sits in build\downloads\omm\extracted; its omm-lib.dll (loaded at run time) ships next to the
# helper.
$omm = Join-Path $root "build\downloads\omm\extracted"
$omm_include = @()
if (Test-Path "$omm\include\omm.h") {
  $omm_include = @("-imsvc$omm\include")
  Copy-Item "$omm\bin\omm-lib.dll" $out -Force
}
# AMD's FSR SDK (its signed DLLs and API headers: MIT, github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK: tag v2.4.0) when it
# sits in build\downloads\fsr\extracted: loaded at run time, its DLLs next to the helper (else none: the helper builds
# without it).
$fsr = Join-Path $root "build\downloads\fsr\extracted\FidelityFX-SDK-2.4.0\Kits\FidelityFX"
$fsr_include = @()
if (Test-Path "$fsr\api\include\ffx_api.h") {
  $fsr_include = @("-imsvc$fsr\api\include", "-imsvc$fsr\upscalers\include", "-imsvc$fsr\framegeneration\include")
  Copy-Item "$fsr\signedbin\amd_fidelityfx_loader_dx12.dll", "$fsr\signedbin\amd_fidelityfx_upscaler_dx12.dll", "$fsr\signedbin\amd_fidelityfx_framegeneration_dx12.dll" $out -Force
}
# Intel's XeSS SDK (Intel Simplified Software License, github.com/intel/xess: its 3.0.2 release) when it sits in
# build\downloads\xess\extracted: loaded at run time, its libxess.dll, libxess_fg.dll and libxell.dll next to the helper.
$xess = Join-Path $root "build\downloads\xess\extracted"
$xess_include = @()
if (Test-Path "$xess\inc\xess\xess_d3d12.h") {
  $xess_include = @("-imsvc$xess\inc")
  Copy-Item "$xess\bin\libxess.dll", "$xess\bin\libxess_fg.dll", "$xess\bin\libxell.dll" $out -Force
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
    & "$root\bin\dxc.exe" -nologo -T cs_6_5 -E main -O3 -I "$nrd\Shaders" -D "NRD_FRONT_$signal" -Fh "$out\rt_nrd_front_$signal.h" -Vn "RT_NRD_FRONT_$signal" "$PSScriptRoot\rt_nrd_front.hlsl"
    & "$root\bin\dxc.exe" -nologo -T cs_6_5 -E main -O3 -I "$nrd\Shaders" -D "NRD_FINISH_$signal" -Fh "$out\rt_nrd_finish_$signal.h" -Vn "RT_NRD_FINISH_$signal" "$PSScriptRoot\rt_nrd_finish.hlsl"
  }
}
# NVIDIA's Nsight Graphics SDK headers (Apache 2.0, include\nsight_graphics_sdk): main.cpp's frame boundaries for Nsight
# Graphics, nothing without it.
& "C:\Program Files\LLVM\bin\clang-cl.exe" /nologo -std:c++20 /O2 /MT /EHsc /DWIN32 /D_WINDOWS -DNOMINMAX `
  -D_ITERATOR_DEBUG_LEVEL=0 "-imsvc$root\include" "-imsvc$root\external\reshade" "-imsvc$root\external\DLSS\include" `
  "-imsvc$root\external\Streamline\include" @nvapi_include @omm_include @fsr_include @xess_include @nrd_include "/I$out" `
  "$PSScriptRoot\main.cpp" /Ferenodx-tw-dlaa64.exe /link /SUBSYSTEM:WINDOWS `
  "$root\external\DLSS\lib\Windows_x86_64\x64\nvsdk_ngx_s.lib" @nvapi_library @nrd_library d3d11.lib d3d12.lib dxgi.lib shell32.lib version.lib user32.lib advapi32.lib wintrust.lib crypt32.lib dbghelp.lib `
  delayimp.lib /DELAYLOAD:dxgi.dll /DELAYLOAD:d3d11.dll /DELAYLOAD:d3d12.dll
