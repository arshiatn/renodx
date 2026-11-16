# Divinity: Original Sin 2 DE's replaced passes, Vanilla exact on WARP: test/tw_d1_post/tests.cpp built with DIVINITY2.
# Each add-on shader compiled with the build's flags against the game's own bytes, and a wrong copy of it that must fail.
# The game's bytes stay out of the repo: -Census names the d2 census's dxbc folder (written from EngineShaders.pak).
# amd64 (the game is 64-bit).
param([string]$Census = (Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\nontw\divinity2\shader_dump\dxbc'))
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$harness = Join-Path $repairRoot 'test\tw_d1_post\tests.cpp'
$testBuild = Join-Path $repairRoot 'build\tw_d2_passes_test_amd64'
$shaders = Join-Path $repairRoot 'src\games\divinity2originalsindefinitive\shaders'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
if (-not (Test-Path -LiteralPath $Census)) { throw "The census's shaders are not at $Census (-Census)" }

# pre: before the tone map (float targets). soft: the PCSS sun shadows, Sun Shadow Softness 2 must change them. pcss:
# their own constants ($constants).
$passes = [ordered]@{
  '772DD22F' = 'pre soft pcss'; '2087D506' = 'pre soft pcss'; '55D4F3B3' = 'pre soft pcss'; '34974AB4' = 'pre soft pcss'
  'E654FCD4' = 'pre soft pcss'; 'B384F6E7' = 'pre soft pcss'; '08D6D72D' = 'pre soft pcss'; 'EBB43131' = 'pre soft pcss'
}
# The PCSS passes' own constants besides the noise (tests.cpp's fourth set: noise never reaches their filter): cb0 the sun
# 0.02 rad across, search width 0.2, every cascade a 0.02-per-unit map at uv 0.25 (inside the first cascade's tile) with
# depth 0.2 per unit and frustums 1 wide; cb12 the view at the origin, 16 x 16 (the orthographic camera's screen: 4 x
# the uv spread).
$identity = '1 0 0 0  0 1 0 0  0 0 1 0  0 0 0 1'
$cascade = '0.02 0 0 0.25  0 0.02 0 0.25  0 0 0.2 0  0 0 0 1'
$pcssGlobals = Join-Path $testBuild 'pcss_cb0.txt'
$pcssView = Join-Path $testBuild 'pcss_cb12.txt'
Set-Content -LiteralPath $pcssGlobals -Value ('1 1 0 0  0.02 0.2 0.1 0  1 1 1 1  1 1 1 1  ' + ((@($identity) * 5) -join '  ') + '  ' + ((@($cascade) * 4) -join '  ')) -Encoding ascii
Set-Content -LiteralPath $pcssView -Value (((@($identity) * 3) -join '  ') + '  0 0 0 0  100 0.1 16 16  1 1 0 0') -Encoding ascii
$constants = @{ 'pcss' = "cb0=$pcssGlobals cb12=$pcssView" }
# The build's flags (CMakeLists.txt): fxc, shader model 5.0, NDEBUG, /Gec, /O3, /Qstrip_reflect.
$fxc = (Select-String -LiteralPath (Join-Path $repairRoot 'build\CMakeCache.txt') -Pattern '^FXC_BIN:FILEPATH=(.*)$').Matches[0].Groups[1].Value
$manifest = @()
foreach ($hash in $passes.Keys) {
  $game = Join-Path $Census "0x$hash.ps_5_0.cso"
  if (-not (Test-Path -LiteralPath $game)) { throw "No game shader 0x$hash in $Census" }
  $source = Get-ChildItem -LiteralPath $shaders -Filter "*0x$hash.ps_5_0.hlsl" | Select-Object -First 1
  if ($null -eq $source) { throw "No add-on shader 0x$hash" }
  # The wrong copy: o0.x * 0.99 + 0.004 at the end of main (before its last return, else before its last brace).
  $text = Get-Content -LiteralPath $source.FullName -Raw
  $at = $text.LastIndexOf('return;')
  if ($at -lt 0) { $at = $text.LastIndexOf('}') }
  $mutantSource = Join-Path $testBuild "mutant_0x$hash.ps_5_0.hlsl"
  Set-Content -LiteralPath $mutantSource -Value ($text.Substring(0, $at) + 'o0.x = o0.x * 0.99 + 0.004; ' + $text.Substring($at)) -NoNewline -Encoding ascii
  $addon = Join-Path $testBuild "addon_0x$hash.cso"
  $mutant = Join-Path $testBuild "mutant_0x$hash.cso"
  # fxc's warnings go to stderr: not errors here (the exit code is). The wrong copy's includes resolve from the shader folder.
  $ErrorActionPreference = 'Continue'
  & $fxc $source.FullName /T ps_5_0 /D __SHADER_TARGET_MAJOR=5 /D __SHADER_TARGET_MINOR=0 /DNDEBUG /Gec /nologo /O3 /Qstrip_reflect /E main /Fo $addon 2>$null | Out-Null
  $addonExit = $LASTEXITCODE
  & $fxc $mutantSource /T ps_5_0 /D __SHADER_TARGET_MAJOR=5 /D __SHADER_TARGET_MINOR=0 /DNDEBUG /Gec /nologo /O3 /Qstrip_reflect /E main /I $shaders /Fo $mutant 2>$null | Out-Null
  $mutantExit = $LASTEXITCODE
  $ErrorActionPreference = 'Stop'
  if ($addonExit -ne 0) { throw "fxc failed: $($source.Name)" }
  if ($mutantExit -ne 0) { throw "fxc failed: the wrong copy of $($source.Name)" }
  $stage, $check, $own = $passes[$hash] -split ' '
  $manifest += "$hash $stage $game $addon $mutant $check $(if ($own) { $constants[$own] })".TrimEnd()
}
$manifestPath = Join-Path $testBuild 'manifest.txt'
Set-Content -LiteralPath $manifestPath -Value $manifest -Encoding ascii

$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 /DDIVINITY2 /I $repairRoot $harness /Fe:d2_passes.exe /link /OPT:REF /OPT:ICF d3d11.lib d3dcompiler.lib dxguid.lib
if ($LASTEXITCODE -ne 0) { throw 'Divinity 2 passes test compile failed' }
& (Join-Path $testBuild 'd2_passes.exe') $manifestPath
if ($LASTEXITCODE -ne 0) { throw 'Divinity 2 passes test failed' }
