# Divinity: Original Sin EE's VFX Brightness patch (src/games/divinityoriginalsinenhanced/vfx_brightness.hpp) on the game's
# own effect shaders on WARP (tests.cpp). The game's bytes stay out of the repo: -Census names the d1_ census's cso folder
# (written from Data\Materials.pak). amd64 (the game is 64-bit).
param([string]$Census = (Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\nontw\divinity1\census\cso'))
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_d1_vfx'
$testBuild = Join-Path $repairRoot 'build\tw_d1_vfx_test_amd64'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
if (-not (Test-Path -LiteralPath $Census)) { throw "The census's shaders are not at $Census (-Census)" }
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /bigobj /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $repairRoot /I "$repairRoot\external\reshade" /I "$repairRoot\external\frozen\include" /I "$repairRoot\external\Detours\include" /I "$repairRoot\external\gtl\include" "$testRoot\tests.cpp" /Fe:d1_vfx.exe /link /OPT:REF /OPT:ICF d3d11.lib dxgi.lib d3dcompiler.lib "$repairRoot\external\Detours\lib.X64\detours.lib" user32.lib advapi32.lib
if ($LASTEXITCODE -ne 0) { throw 'Divinity 1 VFX Brightness test compile failed' }
& (Join-Path $testBuild 'd1_vfx.exe') $Census
if ($LASTEXITCODE -ne 0) { throw 'Divinity 1 VFX Brightness test failed' }
