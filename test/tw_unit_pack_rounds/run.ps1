# Attila's unit motion over the game's repeated bone packs on WARP (tests.cpp). Needs the Attila addon built in build32 (its
# unit motion shaders' .cso under build32\twattila.include\embed) and the game's shader dump
# (bin\cmd_Decompiler-1.3.16\dump_shader_twattila_pack\cso, not in the repository: the game's 0x5F9CD405.vs_4_0.cso).
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'x86')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_unit_pack_rounds'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
$testBuild = Join-Path $repairRoot "build\tw_unit_pack_rounds_test_$Architecture"
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $repairRoot /I $testRoot /I "$repairRoot\external\reshade" /I "$repairRoot\external\frozen\include" /I "$repairRoot\external\Detours\include" /I "$repairRoot\external\gtl\include" "$testRoot\tests.cpp" /Fe:unit_pack_rounds.exe /link /OPT:REF /OPT:ICF user32.lib d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) { throw 'Unit pack rounds test compile failed' }
& (Join-Path $testBuild 'unit_pack_rounds.exe') (Join-Path $repairRoot 'build32\twattila.include\embed') (Join-Path $repairRoot 'bin\cmd_Decompiler-1.3.16\dump_shader_twattila_pack\cso')
if ($LASTEXITCODE -ne 0) { throw 'Unit pack rounds test failed' }
