# Ryse's sun shadow mask passes on WARP with a map that got no casters (Skip the Game's Sun Shadow Maps; tests.cpp). Needs
# the add-on built (the repository's build\rysesonofrome.include\embed\0x<hash>.cso of sunshadow00-04) and -GameShaders:
# the folder of the game's dumped csos (only read; bin\cmd_Decompiler-1.3.16\dump_shader_ryse holds them).
# Then the skip through ReShade's events and command_action under the ray tracing's scene capture (events.cpp: no build of
# the add-on needed). -OutDir: where the test exes go (default build\tw_ryse_shadow_skip_test_<arch>).
param([string]$GameShaders = '', [ValidateSet('x86', 'amd64')][string]$Architecture = 'amd64', [string]$OutDir = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
if ($GameShaders -eq '') { $GameShaders = Join-Path $repairRoot 'bin\cmd_Decompiler-1.3.16\dump_shader_ryse' }
$testRoot = Join-Path $repairRoot 'test\tw_ryse_shadow_skip'
$testBuild = if ($OutDir -ne '') { $OutDir } else { Join-Path $repairRoot "build\tw_ryse_shadow_skip_test_$Architecture" }
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 /I $repairRoot "$testRoot\tests.cpp" /Fe:ryse_shadow_skip.exe /link /OPT:REF /OPT:ICF d3d11.lib d3dcompiler.lib dxgi.lib
if ($LASTEXITCODE -ne 0) { throw 'Ryse shadow skip test compile failed' }
& (Join-Path $testBuild 'ryse_shadow_skip.exe') $repairRoot $GameShaders | Tee-Object -FilePath (Join-Path $testBuild 'last_run.txt')
if ($LASTEXITCODE -ne 0) { throw 'Ryse shadow skip test failed' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /bigobj /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations `
  /I $repairRoot /I "$repairRoot\external\reshade" /I "$repairRoot\external\frozen\include" /I "$repairRoot\external\Detours\include" `
  /I "$repairRoot\external\gtl\include" "$testRoot\events.cpp" /Fe:ryse_shadow_skip_events.exe /link /OPT:REF /OPT:ICF `
  user32.lib advapi32.lib d3d11.lib dxgi.lib d3dcompiler.lib dxguid.lib
if ($LASTEXITCODE -ne 0) { throw 'Ryse shadow skip events test compile failed' }
& (Join-Path $testBuild 'ryse_shadow_skip_events.exe') | Tee-Object -FilePath (Join-Path $testBuild 'last_run_events.txt')
if ($LASTEXITCODE -ne 0) { throw 'Ryse shadow skip events test failed' }
