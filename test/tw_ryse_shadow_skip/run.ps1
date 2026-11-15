# Ryse's sun shadow mask passes on WARP with a map that got no casters (Skip the Game's Sun Shadow Maps; tests.cpp). Needs
# the add-on built (the repository's build\rysesonofrome.include\embed\0x<hash>.cso of sunshadow00-04) and -GameShaders:
# the folder of the game's dumped csos (only read; bin\cmd_Decompiler-1.3.16\dump_shader_ryse holds them).
param([string]$GameShaders = '', [ValidateSet('x86', 'amd64')][string]$Architecture = 'amd64')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
if ($GameShaders -eq '') { $GameShaders = Join-Path $repairRoot 'bin\cmd_Decompiler-1.3.16\dump_shader_ryse' }
$testRoot = Join-Path $repairRoot 'test\tw_ryse_shadow_skip'
$testBuild = Join-Path $repairRoot "build\tw_ryse_shadow_skip_test_$Architecture"
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 /I $repairRoot "$testRoot\tests.cpp" /Fe:ryse_shadow_skip.exe /link /OPT:REF /OPT:ICF d3d11.lib d3dcompiler.lib dxgi.lib
if ($LASTEXITCODE -ne 0) { throw 'Ryse shadow skip test compile failed' }
& (Join-Path $testBuild 'ryse_shadow_skip.exe') $repairRoot $GameShaders | Tee-Object -FilePath (Join-Path $testBuild 'last_run.txt')
if ($LASTEXITCODE -ne 0) { throw 'Ryse shadow skip test failed' }
