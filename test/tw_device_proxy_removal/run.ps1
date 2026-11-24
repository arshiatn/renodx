# utils/device_proxy.hpp's removed-device path on WARP (tests.cpp: a D3D11On12 proxy device over a WARP D3D12 device,
# removed by ID3D12Device5::RemoveDevice), attached through mods::swapchain v2. One process per removal site (resize,
# test, present). No GPU, no game build. Run as x86 and amd64.
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'amd64')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_device_proxy_removal'
$external = Join-Path $repairRoot 'external'
if (-not (Get-ChildItem -LiteralPath (Join-Path $external 'reshade') -Force -ErrorAction SilentlyContinue)) { $external = 'D:\Dev\renodx\external' }
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
# Test programs only in the maintainer's excluded folder.
$testBuild = Join-Path $env:USERPROFILE ".claude\projects\D--Dev-renodx\tools\tw\testbin\tw_device_proxy_removal_$Architecture"
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNDEBUG /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $repairRoot /I $testRoot /I "$external\reshade" /I "$external\reshade\include" /I "$external\frozen\include" /I "$external\Detours\include" /I "$external\gtl\include" "$testRoot\tests.cpp" /Fe:device_proxy_removal.exe /link /OPT:REF /OPT:ICF user32.lib d3d9.lib d3d11.lib d3d12.lib dxgi.lib
if ($LASTEXITCODE -ne 0) { throw 'Device proxy removal test compile failed' }
foreach ($mode in 'resize', 'test', 'present') {
  Write-Output "== $mode"
  & (Join-Path $testBuild 'device_proxy_removal.exe') $mode
  if ($LASTEXITCODE -ne 0) { throw "Device proxy removal test failed ($mode)" }
}
