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
& "$root\bin\dxc.exe" -nologo -T cs_6_5 -E main -O3 -Fh "$out\rt_bounce.h" -Vn RT_BOUNCE "$PSScriptRoot\rt_bounce.cs_6_5.hlsl"
& "$root\bin\dxc.exe" -nologo -T cs_6_9 -E main -O3 -D OPACITY_MICROMAPS -Fh "$out\rt_bounce_omm.h" -Vn RT_BOUNCE_OMM "$PSScriptRoot\rt_bounce.cs_6_5.hlsl"
foreach ($shader in @(@('rt_soft', 'RT_SOFT'), @('rt_scene_data', 'RT_SCENE_DATA'), @('rt_omm', 'RT_OMM'))) {
  & "$root\bin\dxc.exe" -nologo -T cs_6_5 -E main -O3 -Fh "$out\$($shader[0]).h" -Vn $shader[1] "$PSScriptRoot\$($shader[0]).cs_6_5.hlsl"
}
& "C:\Program Files\LLVM\bin\clang-cl.exe" /nologo -std:c++20 /O2 /MT /EHsc /DWIN32 /D_WINDOWS -DNOMINMAX `
  -D_ITERATOR_DEBUG_LEVEL=0 "-imsvc$root\external\reshade" "-imsvc$root\external\DLSS\include" `
  "-imsvc$root\external\Streamline\include" "/I$out" `
  "$PSScriptRoot\main.cpp" /Ferenodx-tw-dlaa64.exe /link /SUBSYSTEM:WINDOWS `
  "$root\external\DLSS\lib\Windows_x86_64\x64\nvsdk_ngx_s.lib" d3d11.lib d3d12.lib dxgi.lib shell32.lib version.lib user32.lib advapi32.lib wintrust.lib crypt32.lib
