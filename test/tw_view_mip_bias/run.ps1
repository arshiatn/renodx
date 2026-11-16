# talesofarise_dlss/constant_mirror.hpp's Texture LOD bias added to the game's MaterialTextureMipBias (tests.cpp, WARP for
# the buffer's desc only). Built over this tree's talesofarise_dlss folder (must pass) and over -Control's (default the main
# checkout's, from before the fix: must fail). x64 (Tales of Arise and Expeditions: Rome are 64-bit).
param([string]$Control = 'D:\Dev\renodx\src\games\talesofarise_dlss')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_view_mip_bias'
$external = Join-Path $repairRoot 'external'
if (-not (Get-ChildItem -LiteralPath (Join-Path $external 'reshade') -Force -ErrorAction SilentlyContinue)) { $external = 'D:\Dev\renodx\external' }
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
# Test programs only in the maintainer's excluded folder.
$testBuild = Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\tw\testbin\tw_view_mip_bias'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
foreach ($build in @(@('view_mip_bias', (Join-Path $repairRoot 'src\games\talesofarise_dlss')), @('view_mip_bias_control', $Control))) {
  & 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $build[1] /I "$external\reshade" /I "$external\reshade\include" "$testRoot\tests.cpp" "/Fe:$($build[0]).exe" /link /OPT:REF /OPT:ICF user32.lib d3d11.lib dxgi.lib
  if ($LASTEXITCODE -ne 0) { throw "View mip bias test compile failed ($($build[0]))" }
}
& (Join-Path $testBuild 'view_mip_bias.exe')
if ($LASTEXITCODE -ne 0) { throw 'View mip bias test failed' }
& (Join-Path $testBuild 'view_mip_bias_control.exe')
if ($LASTEXITCODE -eq 0) { throw 'Control passed: the test does not see the bias replace the game value' }
'Control failed as it must.'
