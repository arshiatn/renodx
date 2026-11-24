# Screen-space shadows' extended march on WARP (tests.cpp) with Attila's addon's shaders (build32\twattila.include: build
# renodx-twattila.addon32 first). First the shader bytes: the plain passes and Shogun 2's own Bend passes compiled from
# this tree and from fc24dbce (the last commit before round v42's phase D) with the build's flags must be the same, but
# sss_setup (its dispatch list moved into sss_dispatch.hlsli), whose output case 1 compares on WARP with fc24dbce's.
# Run as x86 and amd64; -Time also times the rows on the hardware GPU at 4K. -BlockSkipGpu: case 11 on the hardware GPU
# alone (db99b716b's Bend passes against this tree's) and their times at 4K. Both need the gate id (-Gate): the test runs
# under tools\tw\gpu_job.lock (lab\GpuLock.psm1, the gate id in its note), waiting while a TW game or another harness runs.
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'x86', [switch]$Time, [switch]$BlockSkipGpu, [string]$Gate = '')
$ErrorActionPreference = 'Stop'
if (($Time -or $BlockSkipGpu) -and $Gate -eq '') { throw '-Time and -BlockSkipGpu run on the hardware GPU: give their gate id (-Gate)' }
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
# output at the rows' defaults with fc24dbce's on WARP. sss_bend: the block skip and the FarFade skip (bend_sss_gpu.hlsli):
# case 11 compares its output with db99b716b's on WARP.
$allowed = @('sss_bend', 'sss_setup', 'sss_depth', 'sss_depth_ms', 'sss00_0x34ECD06E', 'sss01_0x1B529672', 'sss02_0x78E88E1D')
if (($changed | Where-Object { $_ -notin $allowed }).Count -ne 0) { throw "Plain shader bytes changed: $($changed -join ', ')" }
# Case 11 (the block skip and the FarFade skip): the Bend passes as db99b716b (the commit before them) had them (old_) and
# from this tree (new_), Auto Thickness's the same bytes (both skips compiled out there); this tree's with both skips
# compiled out (noskip_: BEND_SSS_TEST_NO_SKIP) db99b716b's bytes, every pass; this tree's test builds: each pixel's
# writes summed with the skips (wnew_: BEND_SSS_TEST_WRITES, u7) and without (wref_), the skips counted (count_:
# BEND_SSS_TEST_COUNTERS), Rev 0's sign-blind block test (signed_), margins -1e-3 (margin3_) and -0.25 (wmargin25_), every
# block skipped (skipall_, wskipall_).
$e2Before = Join-Path $testBuild 'before_db99b716b'
if (Test-Path -LiteralPath $e2Before) { Remove-Item -LiteralPath $e2Before -Recurse -Force -Confirm:$false }
New-Item -ItemType Directory -Force -Path $e2Before | Out-Null
& git -C $repairRoot archive --output=$archive db99b716b src/games/twattila src/games/twshogun2completeedition src/shaders
if ($LASTEXITCODE -ne 0) { throw 'git archive failed' }
& (Join-Path $env:SystemRoot 'System32\tar.exe') -xf $archive -C $e2Before
if ($LASTEXITCODE -ne 0) { throw 'tar failed' }
$e2Shaders = Join-Path $testBuild 'e2_cso'
New-Item -ItemType Directory -Force -Path $e2Shaders | Out-Null
$bend = [ordered]@{
  'sss_bend' = 'twattila\sss_bend'; 'sss_bend_extended' = 'twattila\sss_bend_extended'; 'sss_bend_extended_find' = 'twattila\sss_bend_extended_find'
  'sss_bend_extended_auto' = 'twattila\sss_bend_extended_auto'; 'sss_bend_extended_auto_find' = 'twattila\sss_bend_extended_auto_find'
  's2_sss00' = 'twshogun2completeedition\shaders\sss00_0x34ECD06E'; 's2_sss01' = 'twshogun2completeedition\shaders\sss01_0x1B529672'
  's2_sss_foliage' = 'twshogun2completeedition\sss_foliage'; 's2_sss_foliage_cascades' = 'twshogun2completeedition\sss_foliage_cascades'
  's2_sss_distance' = 'twshogun2completeedition\sss_distance'; 's2_sss_distance_cascades' = 'twshogun2completeedition\sss_distance_cascades'
}
$skipping = @($bend.Keys | Where-Object { $_ -notlike '*_auto*' })
$attila = @('sss_bend', 'sss_bend_extended', 'sss_bend_extended_find')
$counters = @('/D', 'BEND_SSS_TEST_COUNTERS')
$noSkip = @('/D', 'BEND_SSS_TEST_NO_SKIP')
$writes = @('/D', 'BEND_SSS_TEST_WRITES')
$builds = @(
  @{ Prefix = 'old_'; Root = $e2Before; Names = $bend.Keys; Defines = @() },
  @{ Prefix = 'new_'; Root = $repairRoot; Names = $bend.Keys; Defines = @() },
  @{ Prefix = 'noskip_'; Root = $repairRoot; Names = $bend.Keys; Defines = $noSkip },
  @{ Prefix = 'wnew_'; Root = $repairRoot; Names = $skipping; Defines = $writes },
  @{ Prefix = 'wref_'; Root = $repairRoot; Names = $skipping; Defines = $writes + $noSkip },
  @{ Prefix = 'wskipall_'; Root = $repairRoot; Names = $attila; Defines = $writes + @('/D', 'BEND_SSS_TEST_SKIP_ALL') },
  @{ Prefix = 'count_'; Root = $repairRoot; Names = $skipping; Defines = $counters },
  @{ Prefix = 'signed_'; Root = $repairRoot; Names = $attila; Defines = $counters + @('/D', 'BEND_SSS_TEST_SIGNED') },
  @{ Prefix = 'margin3_'; Root = $repairRoot; Names = $attila; Defines = $counters + @('/D', 'BEND_SSS_TEST_MARGIN=-1e-3') },
  @{ Prefix = 'wmargin25_'; Root = $repairRoot; Names = $attila; Defines = $writes + @('/D', 'BEND_SSS_TEST_MARGIN=-0.25') },
  @{ Prefix = 'skipall_'; Root = $repairRoot; Names = $attila; Defines = @('/D', 'BEND_SSS_TEST_SKIP_ALL') }
)
foreach ($build in $builds) {
  foreach ($name in $build.Names) {
    $defines = $build.Defines
    & $fxc /nologo -T cs_5_0 -E main /O3 /Qstrip_reflect @defines -Fo (Join-Path $e2Shaders "$($build.Prefix)$name.cso") (Join-Path $build.Root "src\games\$($bend[$name]).cs_5_0.hlsl") | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "fxc failed: $($build.Prefix)$name" }
  }
}
$sameBytes = { param($prefix, $name) (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $e2Shaders "old_$name.cso")).Hash -eq (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $e2Shaders "$prefix$name.cso")).Hash }
foreach ($name in @('sss_bend_extended_auto', 'sss_bend_extended_auto_find')) {
  if (-not (& $sameBytes 'new_' $name)) { throw "Auto Thickness's bytes changed since db99b716b: $name" }
}
Write-Host 'Auto Thickness passes the same bytes as db99b716b'
foreach ($name in $bend.Keys) {
  if (-not (& $sameBytes 'noskip_' $name)) { throw "This tree's $name without the skips is not db99b716b's" }
}
Write-Host "Every Bend pass without the skips (BEND_SSS_TEST_NO_SKIP) the same bytes as db99b716b ($($bend.Count))"
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

$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $repairRoot /I "$repairRoot\build32\twattila.include" /I "$repairRoot\external\reshade" /I "$repairRoot\external\frozen\include" /I "$repairRoot\external\Detours\include" /I "$repairRoot\external\gtl\include" "$testRoot\tests.cpp" /Fe:screen_space_shadows.exe /link /OPT:REF /OPT:ICF user32.lib d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) { throw 'Screen-space shadows test compile failed' }
$arguments = @($beforeShaders)
if ($Time) { $arguments += '--time' }
if ($BlockSkipGpu) { $arguments += '--block-skip-gpu' }
$record = $null
if ($Time -or $BlockSkipGpu) {
  Import-Module 'C:\Users\arshi\.claude\projects\D--Dev-renodx\tools\tw\lab\GpuLock.psm1' -Force
  $record = Enter-GpuLock -Script $PSCommandPath -Note "tw_screen_space_shadows gate $Gate $Architecture $($arguments[1..($arguments.Count - 1)] -join ' ')" -Busy
}
try {
  & (Join-Path $testBuild 'screen_space_shadows.exe') @arguments
  $exitCode = $LASTEXITCODE
} finally {
  if ($record) { Exit-GpuLock -Record $record }
}
if ($exitCode -ne 0) { throw 'Screen-space shadows test failed' }
