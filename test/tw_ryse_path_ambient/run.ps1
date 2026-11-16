# Ryse's Path-Traced Ambient and ray-traced AO on WARP (tests.cpp): the tiled lighting with the add-on's black diffuse
# probes, rt_probe_sky, and the ray-traced AO in place of the game's SSDO amount (ssdo00, contact shadows' pass, the
# lighting). Needs the add-on built (the repository's build\rysesonofrome.include\embed\*.cso), -GameShader: the game's
# dumped 0x897232B5.cs_5_0.cso, and -GameSsdo: its 0xD1106E8B.ps_5_0.cso (only read; bin\cmd_Decompiler-1.3.16\
# dump_shader_ryse and dump_shader_ryse2 hold them). -OutDir: where the test exe goes (default
# build\tw_ryse_path_ambient_test_<arch>).
param([string]$GameShader = '', [string]$GameSsdo = '', [ValidateSet('x86', 'amd64')][string]$Architecture = 'amd64', [string]$OutDir = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
if ($GameShader -eq '') { $GameShader = Join-Path $repairRoot 'bin\cmd_Decompiler-1.3.16\dump_shader_ryse\0x897232B5.cs_5_0.cso' }
if ($GameSsdo -eq '') { $GameSsdo = Join-Path $repairRoot 'bin\cmd_Decompiler-1.3.16\dump_shader_ryse2\0xD1106E8B.ps_5_0.cso' }
$testRoot = Join-Path $repairRoot 'test\tw_ryse_path_ambient'
$testBuild = if ($OutDir -ne '') { $OutDir } else { Join-Path $repairRoot "build\tw_ryse_path_ambient_test_$Architecture" }
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 /I $repairRoot "$testRoot\tests.cpp" /Fe:ryse_path_ambient.exe /link /OPT:REF /OPT:ICF d3d11.lib d3dcompiler.lib dxgi.lib
if ($LASTEXITCODE -ne 0) { throw 'Ryse path ambient test compile failed' }
& (Join-Path $testBuild 'ryse_path_ambient.exe') $repairRoot $GameShader $GameSsdo | Tee-Object -FilePath (Join-Path $testBuild 'last_run.txt')
if ($LASTEXITCODE -ne 0) { throw 'Ryse path ambient test failed' }
