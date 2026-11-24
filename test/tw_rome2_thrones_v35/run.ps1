# PsychoV35's V35 fit for Rome II and Thrones of Britannia on WARP (tests.cpp; ~11k candidates a game, two draws each).
# Compiles both games' tone mappers (t00 -> t01) from this tree with the build's flags, reflection kept (tests.cpp binds
# by name, through test/tw_psychov35's chains), then builds and runs the test. -OutDir: where the test exe is built and
# run (ATN's antivirus-excluded test folder, tools\tw\testbin); else build\. Needs a configured build32 or build (fxc).
param([string]$OutDir = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_rome2_thrones_v35'
$testBuild = if ($OutDir) { $OutDir } else { Join-Path $repairRoot 'build\tw_rome2_thrones_v35_test' }
$shaders = Join-Path $testBuild 'cso'
New-Item -ItemType Directory -Force -Path $shaders | Out-Null

$cache = Join-Path $repairRoot 'build32\CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cache)) { $cache = Join-Path $repairRoot 'build\CMakeCache.txt' }
$fxc = (Select-String -LiteralPath $cache -Pattern '^FXC_BIN:FILEPATH=(.*)$').Matches[0].Groups[1].Value
$toneMappers = [ordered]@{
  'r_t00' = 'twrome2\shaders\t00_0xEE1DA45C.ps_4_0.hlsl'; 'r_t01' = 'twrome2\shaders\t01_0x850AEE44.ps_4_0.hlsl'
  'th_t00' = 'twthronesofbritannia\shaders\t00_0x8BF6B657.ps_4_0.hlsl'; 'th_t01' = 'twthronesofbritannia\shaders\t01_0xD0FAF089.ps_4_0.hlsl'
}
# The build's flags (CMakeLists.txt) without /Qstrip_reflect. fxc's warnings go to stderr: the exit code decides.
$ErrorActionPreference = 'Continue'
foreach ($name in $toneMappers.Keys) {
  & $fxc (Join-Path $repairRoot "src\games\$($toneMappers[$name])") /T ps_4_0 /D __SHADER_TARGET_MAJOR=4 /D __SHADER_TARGET_MINOR=0 `
      /DNDEBUG /Gec /nologo /O3 /E main /Fo (Join-Path $shaders "$name.cso") 2>$null | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "fxc failed: $($toneMappers[$name])" }
}

$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 `
    -Wno-macro-redefined /I $repairRoot "$testRoot\tests.cpp" /Fe:rome2_thrones_v35.exe /link /OPT:REF /OPT:ICF d3d11.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) { throw 'Rome II / Thrones V35 fit test compile failed' }
& (Join-Path $testBuild 'rome2_thrones_v35.exe') $shaders
if ($LASTEXITCODE -ne 0) { throw 'Rome II / Thrones V35 fit test failed' }
