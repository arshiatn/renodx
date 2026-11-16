# Divinity 2's helper route on WARP (tests.cpp): DLAA's camera hooks and shaders, the game's SMAA / FXAA giving way, the
# rays' writes read by the game's own PPApplyAO, with the addon's own shaders (build\divinity2originalsindefinitive.include:
# build divinity2originalsindefinitive first). The game's PPApplyAO stays out of the repo: -Census names the d2 census's
# dxbc folder (written from EngineShaders.pak). The game is 64-bit: amd64 only.
param([string]$Census = (Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\nontw\divinity2\shader_dump\dxbc'))
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_d2_helper'
$testBuild = Join-Path $repairRoot 'build\tw_d2_helper_test'
$applyAo = Join-Path $Census '0xF3A764F3.ps_5_0.cso'
if (-not (Test-Path -LiteralPath $applyAo)) { throw "The game's PPApplyAO is not at $applyAo (-Census)" }
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $repairRoot /I "$repairRoot\build\divinity2originalsindefinitive.include" /I "$repairRoot\external\reshade" /I "$repairRoot\external\frozen\include" /I "$repairRoot\external\Detours\include" /I "$repairRoot\external\gtl\include" "$testRoot\tests.cpp" /Fe:d2_helper.exe /link /OPT:REF /OPT:ICF user32.lib d3d11.lib dxgi.lib d3dcompiler.lib "$repairRoot\external\Detours\lib.X64\detours.lib"
if ($LASTEXITCODE -ne 0) { throw 'Divinity 2 helper test compile failed' }
& (Join-Path $testBuild 'd2_helper.exe') $applyAo
if ($LASTEXITCODE -ne 0) { throw 'Divinity 2 helper test failed' }
