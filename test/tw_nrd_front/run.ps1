# NRD's view depth from the helper's NRD inputs (tests.cpp): dlaa_helper/rt_nrd_front.hlsl's AMBIENT and BOUNCE passes on
# WARP (and its CONTROL build, -D NRD_FRONT_DISTANCE_Z, must fail). amd64.
# -Bin: the folder of dxcompiler.dll and dxil.dll (the repository's bin; a worktree without one: the main checkout's).
# -Nrd: NRD's Shaders folder (build\downloads\nrd\NRD\Shaders, as build.ps1's).
param([string]$Bin = '', [string]$Nrd = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_nrd_front'
if ($Bin -eq '') { $Bin = Join-Path $repairRoot 'bin' }
if ($Nrd -eq '') { $Nrd = Join-Path $repairRoot 'build\downloads\nrd\NRD\Shaders' }
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
# Test programs only in the maintainer's excluded folder.
$testBuild = Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\tw\testbin\tw_nrd_front'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX "$testRoot\tests.cpp" /Fe:nrd_front.exe /link d3d12.lib dxgi.lib dxguid.lib
if ($LASTEXITCODE -ne 0) { throw 'NRD front test compile failed' }
& (Join-Path $testBuild 'nrd_front.exe') $repairRoot $Bin $Nrd | Tee-Object -FilePath (Join-Path $testBuild 'last_run.txt')
if ($LASTEXITCODE -ne 0) { throw 'NRD front test failed' }
