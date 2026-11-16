# Ryse's Shadow Casters Out of View on WARP (tests.cpp): the game's sun caster vertex shaders with rt_core's cascade buffer at
# PER_FRAME, streamed out through the outside-view geometry shaders. -Dumps: the folder with the dumped 0x7EB6531F.vs_5_0.cso
# and 0x48CE820A.vs_5_0.cso (default bin\cmd_Decompiler-1.3.16\dump_shader_ryse; only read). -OutDir: where the test exe
# goes (default build\tw_ryse_cascade_casters_test_<arch>).
param([string]$Dumps = '', [ValidateSet('x86', 'amd64')][string]$Architecture = 'amd64', [string]$OutDir = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
if ($Dumps -eq '') { $Dumps = Join-Path $repairRoot 'bin\cmd_Decompiler-1.3.16\dump_shader_ryse' }
$testRoot = Join-Path $repairRoot 'test\tw_ryse_cascade_casters'
$testBuild = if ($OutDir -ne '') { $OutDir } else { Join-Path $repairRoot "build\tw_ryse_cascade_casters_test_$Architecture" }
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 /I $repairRoot "$testRoot\tests.cpp" /Fe:ryse_cascade_casters.exe /link /OPT:REF /OPT:ICF d3d11.lib dxgi.lib
if ($LASTEXITCODE -ne 0) { throw 'Ryse cascade casters test compile failed' }
& (Join-Path $testBuild 'ryse_cascade_casters.exe') $repairRoot $Dumps | Tee-Object -FilePath (Join-Path $testBuild 'last_run.txt')
if ($LASTEXITCODE -ne 0) { throw 'Ryse cascade casters test failed' }
