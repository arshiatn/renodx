# A compact draw's cluster set when the game writes other indices into its index buffer's range (tests.cpp, WARP):
# rt_core's PrepareClusterSet and its write hooks. rt_core.hpp includes the addons' embedded shaders: x86 takes
# build32\twattila.include (build renodx-twattila.addon32 first); amd64 takes build\ruse.include (build
# renodx-ruse.addon64 first), which also holds R.U.S.E.'s replay shaders for case 8 (ruse/bridge_replay.hpp, with MinHook).
# Run as x86 and amd64.
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'amd64')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_cluster_rewrite'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
$testBuild = Join-Path $repairRoot "build\tw_cluster_rewrite_test_$Architecture"
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
$detoursArch = if ($Architecture -eq 'x86') { 'X86' } else { 'X64' }
$game = if ($Architecture -eq 'x86') {
  @('/I', "$repairRoot\build32\twattila.include")
} else {
  @('/I', "$repairRoot\build\ruse.include", '/I', "$repairRoot\external\minhook\include", '/DRENODX_TEST_RUSE_REPLAY')
}
$libs = if ($Architecture -eq 'x86') { @() } else { @("$repairRoot\build\external\minhook\Release\minhook.x64.lib", 'd3d9.lib') }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /bigobj /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $repairRoot @game /I "$repairRoot\external\reshade" /I "$repairRoot\external\frozen\include" /I "$repairRoot\external\Detours\include" /I "$repairRoot\external\gtl\include" "$testRoot\tests.cpp" /Fe:cluster_rewrite.exe /link /OPT:REF /OPT:ICF "$repairRoot\external\Detours\lib.$detoursArch\detours.lib" @libs user32.lib advapi32.lib d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib dxguid.lib
if ($LASTEXITCODE -ne 0) { throw 'Cluster rewrite test compile failed' }
& (Join-Path $testBuild 'cluster_rewrite.exe')
if ($LASTEXITCODE -ne 0) { throw 'Cluster rewrite test failed' }
