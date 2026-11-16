# Divinity: Original Sin EE's tone map on WARP (tests.cpp): the game's own 0x30A1D40D against the add-on's, compiled here
# with the build's flags. The game's bytes stay out of the repo: -GameShader names them (the d1_ census writes
# census\cso\0x30A1D40D.ps_5_0.cso from Data\EngineShaders.pak). amd64 (the game is 64-bit).
param([string]$GameShader = (Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\nontw\divinity1\census\cso\0x30A1D40D.ps_5_0.cso'))
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_divinity1'
$testBuild = Join-Path $repairRoot 'build\tw_divinity1_test_amd64'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
if (-not (Test-Path -LiteralPath $GameShader)) { throw "The game's tone map is not at $GameShader (-GameShader)" }

# The build's flags (CMakeLists.txt): fxc, shader model 5.0, NDEBUG, /Gec, /O3, /Qstrip_reflect.
$fxc = (Select-String -LiteralPath (Join-Path $repairRoot 'build\CMakeCache.txt') -Pattern '^FXC_BIN:FILEPATH=(.*)$').Matches[0].Groups[1].Value
function Compile([string]$Source, [string]$Target, [string]$Entry, [string]$Output, [string[]]$Extra = @()) {
  # fxc's warnings go to stderr: not errors here (the exit code is).
  $ErrorActionPreference = 'Continue'
  & $fxc $Source /T $Target /D __SHADER_TARGET_MAJOR=5 /D __SHADER_TARGET_MINOR=0 /DNDEBUG /Gec /nologo /O3 /E $Entry @Extra /Fo $Output 2>$null | Out-Null
  $ErrorActionPreference = 'Stop'
  if ($LASTEXITCODE -ne 0) { throw "fxc failed: $Source ($Entry)" }
}
$addonShader = Join-Path $testBuild 'addon_0x30A1D40D.cso'
Compile (Join-Path $repairRoot 'src\games\divinityoriginalsinenhanced\shaders\0x30A1D40D.ps_5_0.hlsl') ps_5_0 main $addonShader @('/Qstrip_reflect')
# The add-on's tone map before the smooth grade shoulder (a6762afab^: the knee at 1, the bytes Smooth Grade Shoulder off
# must give), from git with the files it includes.
$oldRoot = Join-Path $testBuild 'before_shoulder'
$oldTar = Join-Path $testBuild 'before_shoulder.tar'
if (Test-Path -LiteralPath $oldRoot) { Remove-Item -LiteralPath $oldRoot -Recurse -Force -Confirm:$false }
New-Item -ItemType Directory -Force -Path $oldRoot | Out-Null
& git -C $repairRoot archive --format=tar -o $oldTar 'a6762afab^' -- src/shaders src/games/divinityoriginalsinenhanced/shaders `
    src/games/divinityoriginalsinenhanced/shared.h src/games/divinityoriginalsinenhanced/psycho_test30.hlsli `
    src/games/twattila/psycho35.hlsli src/games/twattila/psycho_test35.hlsli
if ($LASTEXITCODE -ne 0) { throw 'git archive failed (a6762afab^)' }
& tar -xf $oldTar -C $oldRoot
if ($LASTEXITCODE -ne 0) { throw 'tar failed (a6762afab^)' }
$oldShader = Join-Path $testBuild 'before_shoulder_0x30A1D40D.cso'
Compile (Join-Path $oldRoot 'src\games\divinityoriginalsinenhanced\shaders\0x30A1D40D.ps_5_0.hlsl') ps_5_0 main $oldShader @('/Qstrip_reflect')
# The grade alone, with the add-on's shader folder as the include path: the row on, off, and its control (the knee at 1).
$gradeShader = Join-Path $testBuild 'grade.cso'
$gradeOffShader = Join-Path $testBuild 'grade_off.cso'
$controlShader = Join-Path $testBuild 'grade_control.cso'
foreach ($entry in @(@('main', $gradeShader), @('off', $gradeOffShader), @('control', $controlShader))) {
  Compile (Join-Path $testRoot 'grade.hlsl') cs_5_0 $entry[0] $entry[1] @('/I', (Join-Path $repairRoot 'src\games\divinityoriginalsinenhanced\shaders'))
}

$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 /I $repairRoot "$testRoot\tests.cpp" /Fe:divinity1.exe /link /OPT:REF /OPT:ICF d3d11.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) { throw 'Divinity 1 tone map test compile failed' }
& (Join-Path $testBuild 'divinity1.exe') $GameShader $addonShader $gradeShader $controlShader $gradeOffShader $oldShader
if ($LASTEXITCODE -ne 0) { throw 'Divinity 1 tone map test failed' }
