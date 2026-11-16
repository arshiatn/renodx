# Divinity: Original Sin EE's helper inputs and ray-traced replacements on WARP (tests.cpp): the add-on's d1_ shaders
# compiled with the build's flags, the game's own sun pass (0xCCCB7253 DeferredDirectionalWithSSAO) reading what they
# write. The game's bytes stay out of the repo: -Census names the d1_ census's cso folder. amd64 (the game is 64-bit).
param([string]$Census = (Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\nontw\divinity1\census\cso'))
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_d1_rays'
$testBuild = Join-Path $repairRoot 'build\tw_d1_rays_test_amd64'
$source = Join-Path $repairRoot 'src\games\divinityoriginalsinenhanced'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$sunPass = Join-Path $Census '0xCCCB7253.ps_5_0.cso'
if (-not (Test-Path -LiteralPath $sunPass)) { throw "The game's sun pass is not at $sunPass (-Census)" }

# The build's flags (CMakeLists.txt): fxc, shader model 5.0, NDEBUG, /Gec, /O3, /Qstrip_reflect.
$fxc = (Select-String -LiteralPath (Join-Path $repairRoot 'build\CMakeCache.txt') -Pattern '^FXC_BIN:FILEPATH=(.*)$').Matches[0].Groups[1].Value
$shaders = [ordered]@{
  'd1_upscale_inputs' = 'cs_5_0'; 'd1_upscale_output' = 'ps_5_0'; 'd1_rt_inputs' = 'cs_5_0'; 'd1_rt_guides' = 'cs_5_0'
  'd1_rt_shadow' = 'ps_5_0'; 'd1_rt_ambient' = 'ps_5_0'; 'light_bounce_vs' = 'vs_5_0'
}
$arguments = @($sunPass)
foreach ($name in $shaders.Keys) {
  $target = $shaders[$name]
  $output = Join-Path $testBuild "$name.cso"
  # fxc's warnings go to stderr: not errors here (the exit code is).
  $ErrorActionPreference = 'Continue'
  & $fxc (Join-Path $source "$name.$target.hlsl") /T $target /D __SHADER_TARGET_MAJOR=5 /D __SHADER_TARGET_MINOR=0 /DNDEBUG /Gec /nologo /O3 /Qstrip_reflect /E main /Fo $output 2>$null | Out-Null
  $ErrorActionPreference = 'Stop'
  if ($LASTEXITCODE -ne 0) { throw "fxc failed: $name" }
  $arguments += $output
}

$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 /I $repairRoot "$testRoot\tests.cpp" /Fe:d1_rays.exe /link /OPT:REF /OPT:ICF d3d11.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) { throw 'Divinity 1 rays test compile failed' }
& (Join-Path $testBuild 'd1_rays.exe') @arguments
if ($LASTEXITCODE -ne 0) { throw 'Divinity 1 rays test failed' }
