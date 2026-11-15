// Path tracing and reflections at the lighting pass (rt_path.hlsli) as a DXR 1.2 ray generation shader: shader
// execution reordering after each path's first hit, opacity micromaps in its ray queries (shader model 6.9). CMake
// does not build ray tracing libraries: 0x7A1E0D1C.cso next to this file is its build, embedded as it is. Rebuild it
// after changing this file or what it includes (rt_path.hlsli, rt_cache.hlsli, rt_scene.hlsli, rt_surface.hlsli):
//   bin\dxc.exe -nologo -T lib_6_9 -O3 -Qstrip_reflect -D __SHADER_TARGET_MAJOR=6 -D __SHADER_TARGET_MINOR=9
//     src\games\talesofarise_dlss\0x7A1E0D1C.lib_6_9.hlsl -Fo src\games\talesofarise_dlss\0x7A1E0D1C.cso
#define PATH_REORDER
#include "./rt_path.hlsli"

[shader("raygeneration")]
void PathTraceRayGen() {
  PathTracePixel(DispatchRaysIndex().xy);
}
