# Ryse's ray-traced specular occlusion merged into the game's screen-space reflections, through the tiled lighting's use
# of them, on WARP (tests.cpp). Needs the add-on built (the repository's build\rysesonofrome.include\embed\
# rt_specular_merge.cso). -OutDir: where the test exe goes (default build\tw_ryse_specular_merge_test_<arch>).
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'amd64', [string]$OutDir = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_ryse_specular_merge'
$testBuild = if ($OutDir -ne '') { $OutDir } else { Join-Path $repairRoot "build\tw_ryse_specular_merge_test_$Architecture" }
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 /I $repairRoot "$testRoot\tests.cpp" /Fe:ryse_specular_merge.exe /link /OPT:REF /OPT:ICF d3d11.lib d3dcompiler.lib dxgi.lib
if ($LASTEXITCODE -ne 0) { throw 'Ryse specular merge test compile failed' }
& (Join-Path $testBuild 'ryse_specular_merge.exe') $repairRoot | Tee-Object -FilePath (Join-Path $testBuild 'last_run.txt')
if ($LASTEXITCODE -ne 0) { throw 'Ryse specular merge test failed' }
