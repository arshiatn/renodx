# Ryse's tonemap00 SDR encode against the game's bytes on WARP (tests.cpp). Needs the add-on built (the repository's
# build\rysesonofrome.include\embed\0xB9B672AA.cso) and -GameShader: the game's dumped 0xB9B672AA.ps_5_0.cso (only read;
# bin\cmd_Decompiler-1.3.16\dump_shader_ryse holds it).
param([string]$GameShader = '', [ValidateSet('x86', 'amd64')][string]$Architecture = 'amd64')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
if ($GameShader -eq '') { $GameShader = Join-Path $repairRoot 'bin\cmd_Decompiler-1.3.16\dump_shader_ryse\0xB9B672AA.ps_5_0.cso' }
$testRoot = Join-Path $repairRoot 'test\tw_ryse_tonemap'
$testBuild = Join-Path $repairRoot "build\tw_ryse_tonemap_test_$Architecture"
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 /I $repairRoot "$testRoot\tests.cpp" /Fe:ryse_tonemap.exe /link /OPT:REF /OPT:ICF d3d11.lib d3dcompiler.lib dxgi.lib
if ($LASTEXITCODE -ne 0) { throw 'Ryse tonemap test compile failed' }
& (Join-Path $testBuild 'ryse_tonemap.exe') $repairRoot $GameShader | Tee-Object -FilePath (Join-Path $testBuild 'last_run.txt')
if ($LASTEXITCODE -ne 0) { throw 'Ryse tonemap test failed' }
