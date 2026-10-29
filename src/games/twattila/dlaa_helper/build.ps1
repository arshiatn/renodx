# Builds renodx-tw-dlaa64.exe (the Total War DLAA helper, x64; see main.cpp) into build\tw_dlaa_helper (or the folder
# given as the first argument). It is not a CMake target (the repository's targets are addons); ship it next to the addon
# with nvngx_dlss.dll (and nvngx_dlssd.dll for Ray Reconstruction; for frame generation Streamline's sl.interposer.dll,
# sl.common.dll, sl.dlss_g.dll, sl.reflex.dll, sl.pcl.dll and nvngx_dlssg.dll; for DXR 1.2 the Agility SDK's
# D3D12\D3D12Core.dll). The ray tracing shaders (rt_*.cs_6_5.hlsl; rt_trace also for shader model 6.9 with opacity
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
& "$root\bin\dxc.exe" -nologo -T cs_6_0 -E main -O3 -Fh "$out\fg_ui.h" -Vn FG_UI "$PSScriptRoot\fg_ui.cs_6_0.hlsl"
foreach ($shader in @(@('rt_soft', 'RT_SOFT'), @('rt_scene_data', 'RT_SCENE_DATA'), @('rt_compact', 'RT_COMPACT'), @('rt_place', 'RT_PLACE'),
                      @('rt_cache_fill', 'RT_CACHE_FILL'), @('rt_cache_resolve', 'RT_CACHE_RESOLVE'), @('rt_reflect_filter', 'RT_REFLECT_FILTER'))) {
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
& "C:\Program Files\LLVM\bin\clang-cl.exe" /nologo -std:c++20 /O2 /MT /EHsc /DWIN32 /D_WINDOWS -DNOMINMAX `
  -D_ITERATOR_DEBUG_LEVEL=0 "-imsvc$root\external\reshade" "-imsvc$root\external\DLSS\include" `
  "-imsvc$root\external\Streamline\include" @nvapi_include @omm_include "/I$out" `
  "$PSScriptRoot\main.cpp" /Ferenodx-tw-dlaa64.exe /link /SUBSYSTEM:WINDOWS `
  "$root\external\DLSS\lib\Windows_x86_64\x64\nvsdk_ngx_s.lib" @nvapi_library d3d11.lib d3d12.lib dxgi.lib shell32.lib version.lib user32.lib advapi32.lib wintrust.lib crypt32.lib
