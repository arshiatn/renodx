# PsychoV30's NaN / infinity guards on WARP (tests.cpp): twattila/psycho_test30.hlsli compiled with the build's flags as
# shipped and without its guards (the control). amd64.
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_psychov30_nan'
$testBuild = Join-Path $repairRoot 'build\tw_psychov30_nan_test_amd64'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null

# The build's flags (CMakeLists.txt): fxc, shader model 5.0, NDEBUG, /Gec, /O3, /Qstrip_reflect.
$fxc = (Select-String -LiteralPath (Join-Path $repairRoot 'build\CMakeCache.txt') -Pattern '^FXC_BIN:FILEPATH=(.*)$').Matches[0].Groups[1].Value
$ours = Join-Path $testBuild 'v30.cso'
$control = Join-Path $testBuild 'v30_no_guards.cso'
$oursGuards = Join-Path $testBuild 'guards.cso'
$controlGuards = Join-Path $testBuild 'guards_no_guards.cso'
foreach ($build in @(@($ours, 'main', @()), @($control, 'main', @('/D', 'NO_GUARDS')),
                     @($oursGuards, 'guards', @()), @($controlGuards, 'guards', @('/D', 'NO_GUARDS')))) {
  # fxc's warnings go to stderr: not errors here (the exit code is).
  $ErrorActionPreference = 'Continue'
  & $fxc (Join-Path $testRoot 'v30.hlsl') /T cs_5_0 /D __SHADER_TARGET_MAJOR=5 /D __SHADER_TARGET_MINOR=0 /DNDEBUG /Gec /nologo /O3 /Qstrip_reflect @($build[2]) /E $build[1] /Fo $build[0] 2>$null | Out-Null
  $ErrorActionPreference = 'Stop'
  if ($LASTEXITCODE -ne 0) { throw "fxc failed: v30.hlsl ($($build[0]))" }
}

$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 "$testRoot\tests.cpp" /Fe:psychov30_nan.exe /link /OPT:REF /OPT:ICF d3d11.lib
if ($LASTEXITCODE -ne 0) { throw 'PsychoV30 NaN test compile failed' }
& (Join-Path $testBuild 'psychov30_nan.exe') $ours $control $oursGuards $controlGuards
if ($LASTEXITCODE -ne 0) { throw 'PsychoV30 NaN test failed' }
