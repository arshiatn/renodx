# Screen-space shadows' extended march on WARP (tests.cpp) with Attila's addon's shaders (build32\twattila.include: build
# renodx-twattila.addon32 first). First the shader bytes: the plain passes and Shogun 2's own Bend passes compiled from
# this tree and from fc24dbce (the last commit before round v42's phase D) with the build's flags must be the same, but
# sss_setup (its dispatch list moved into sss_dispatch.hlsli), whose output case 1 compares on WARP with fc24dbce's.
# Run as x86 and amd64; -Time also times the rows on the hardware GPU at 4K.
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'x86', [switch]$Time)
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_screen_space_shadows'
$testBuild = Join-Path $repairRoot "build\tw_screen_space_shadows_test_$Architecture"
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null

$fxc = (Select-String -LiteralPath (Join-Path $repairRoot 'build32\CMakeCache.txt') -Pattern '^FXC_BIN:FILEPATH=(.*)$').Matches[0].Groups[1].Value
$before = Join-Path $testBuild 'before'
if (Test-Path -LiteralPath $before) { Remove-Item -LiteralPath $before -Recurse -Force -Confirm:$false }
New-Item -ItemType Directory -Force -Path $before | Out-Null
$archive = Join-Path $testBuild 'before.tar'
& git -C $repairRoot archive --output=$archive fc24dbce src/games/twattila src/games/twshogun2completeedition src/shaders
if ($LASTEXITCODE -ne 0) { throw 'git archive failed' }
& (Join-Path $env:SystemRoot 'System32\tar.exe') -xf $archive -C $before
if ($LASTEXITCODE -ne 0) { throw 'tar failed' }
$shaders = @('twattila\sss_bend.cs_5_0', 'twattila\sss_setup.cs_5_0', 'twattila\sss_blur.cs_5_0', 'twattila\sss_depth.cs_5_0',
             'twattila\sss_depth_ms.cs_5_0', 'twattila\sss_debug.cs_5_0', 'twshogun2completeedition\shaders\sss00_0x34ECD06E.cs_5_0',
             'twshogun2completeedition\shaders\sss01_0x1B529672.cs_5_0', 'twshogun2completeedition\shaders\sss02_0x78E88E1D.cs_5_0')
$beforeShaders = Join-Path $testBuild 'before_cso'
$afterShaders = Join-Path $testBuild 'after_cso'
New-Item -ItemType Directory -Force -Path $beforeShaders, $afterShaders | Out-Null
$changed = @()
foreach ($shader in $shaders) {
  $name = (Split-Path -Leaf $shader) -replace '\.cs_5_0$', ''
  foreach ($side in @(@{ Root = (Join-Path $before 'src\games'); Out = $beforeShaders }, @{ Root = (Join-Path $repairRoot 'src\games'); Out = $afterShaders })) {
    & $fxc /nologo -T cs_5_0 -E main /O3 /Qstrip_reflect -Fo (Join-Path $side.Out "$name.cso") (Join-Path $side.Root "$shader.hlsl") | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "fxc failed: $shader" }
  }
  $a = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $beforeShaders "$name.cso")).Hash
  $b = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $afterShaders "$name.cso")).Hash
  if ($a -ne $b) { $changed += $name }
}
Write-Host "Shader bytes changed since fc24dbce: $($changed -join ', ')"
# Shogun 2's extended passes (Softer Away from the Caster, Light Through Banners, Coloured Shadows; case 10): this tree's only,
# they are newer than fc24dbce.
foreach ($shader in @('sss_distance.cs_5_0', 'sss_distance_cascades.cs_5_0')) {
  $name = $shader -replace '\.cs_5_0$', ''
  & $fxc /nologo -T cs_5_0 -E main /O3 /Qstrip_reflect -Fo (Join-Path $afterShaders "$name.cso") (Join-Path $repairRoot "src\games\twshogun2completeedition\$shader.hlsl") | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "fxc failed: $shader" }
}
# sss_depth (and its MSAA form): the G-buffer's foliage mark (Warhammer III); its output against fc24dbce's on WARP too.
# Shogun 2's sss00-02: Bend's parameters as rows and Light Through Foliage (its shader_injection grew): case 8 compares their
# output at the rows' defaults with fc24dbce's on WARP.
# sss_bend: its plain march takes the counts in fours too (X27); case 1 compares it with fc24dbce's at 100% (counts in fours)
# to the bit, and below 100% against fc24dbce's with the odd counts (its CONTROL). O-SS5 (bend_sss_gpu.hlsli's screen steps)
# changes sss_bend's and sss00-02's bytes too: case 1 and case 8 hold them to the bit at 100% / Shogun 2's step 1.
$allowed = @('sss_setup', 'sss_depth', 'sss_depth_ms', 'sss_bend', 'sss00_0x34ECD06E', 'sss01_0x1B529672', 'sss02_0x78E88E1D')
if (($changed | Where-Object { $_ -notin $allowed }).Count -ne 0) { throw "Plain shader bytes changed: $($changed -join ', ')" }
# sss_lights as f6408aa1 had it (before its repeat test read a light's position only where the scores match): case 9
# compares its output with today's on WARP, --time both on the hardware GPU.
$lightsBefore = Join-Path $testBuild 'before_f6408aa1'
if (Test-Path -LiteralPath $lightsBefore) { Remove-Item -LiteralPath $lightsBefore -Recurse -Force -Confirm:$false }
New-Item -ItemType Directory -Force -Path $lightsBefore | Out-Null
& git -C $repairRoot archive --output=$archive f6408aa1 src/games/twattila src/shaders
if ($LASTEXITCODE -ne 0) { throw 'git archive failed' }
& (Join-Path $env:SystemRoot 'System32\tar.exe') -xf $archive -C $lightsBefore
if ($LASTEXITCODE -ne 0) { throw 'tar failed' }
& $fxc /nologo -T cs_5_0 -E main /O3 /Qstrip_reflect -Fo (Join-Path $beforeShaders 'sss_lights_f6408aa1.cso') (Join-Path $lightsBefore 'src\games\twattila\sss_lights.cs_5_0.hlsl') | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'fxc failed: f6408aa1 sss_lights' }
# Case 11 (O-SS5): sss_bend and sss_bend_extended_find as bc423ef9d had them (before O-SS5: a sample a render pixel; control
# C and W1's today) and this tree's with BEND_SSS_STEP_CONTROL 1, 2, 4 (sss_bend) and 3 (the find variant): the W tests'
# CONTROLS.
$stepsBefore = Join-Path $testBuild 'before_bc423ef9d'
if (Test-Path -LiteralPath $stepsBefore) { Remove-Item -LiteralPath $stepsBefore -Recurse -Force -Confirm:$false }
New-Item -ItemType Directory -Force -Path $stepsBefore | Out-Null
& git -C $repairRoot archive --output=$archive bc423ef9d src/games/twattila src/shaders
if ($LASTEXITCODE -ne 0) { throw 'git archive failed' }
& (Join-Path $env:SystemRoot 'System32\tar.exe') -xf $archive -C $stepsBefore
if ($LASTEXITCODE -ne 0) { throw 'tar failed' }
foreach ($shader in @('sss_bend', 'sss_bend_extended_find')) {
  & $fxc /nologo -T cs_5_0 -E main /O3 /Qstrip_reflect -Fo (Join-Path $beforeShaders "${shader}_bc423ef9d.cso") (Join-Path $stepsBefore "src\games\twattila\$shader.cs_5_0.hlsl") | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "fxc failed: bc423ef9d $shader" }
}
foreach ($control in @(@{ Shader = 'sss_bend'; Value = 1 }, @{ Shader = 'sss_bend'; Value = 2 }, @{ Shader = 'sss_bend_extended_find'; Value = 3 }, @{ Shader = 'sss_bend'; Value = 4 })) {
  & $fxc /nologo -T cs_5_0 -E main /O3 /Qstrip_reflect /D "BEND_SSS_STEP_CONTROL=$($control.Value)" -Fo (Join-Path $beforeShaders "$($control.Shader)_control$($control.Value).cso") (Join-Path $repairRoot "src\games\twattila\$($control.Shader).cs_5_0.hlsl") | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "fxc failed: $($control.Shader) control $($control.Value)" }
}

$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $repairRoot /I "$repairRoot\build32\twattila.include" /I "$repairRoot\external\reshade" /I "$repairRoot\external\frozen\include" /I "$repairRoot\external\Detours\include" /I "$repairRoot\external\gtl\include" "$testRoot\tests.cpp" /Fe:screen_space_shadows.exe /link /OPT:REF /OPT:ICF user32.lib d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) { throw 'Screen-space shadows test compile failed' }
$arguments = @($beforeShaders)
if ($Time) { $arguments += '--time' }
& (Join-Path $testBuild 'screen_space_shadows.exe') @arguments
if ($LASTEXITCODE -ne 0) { throw 'Screen-space shadows test failed' }
