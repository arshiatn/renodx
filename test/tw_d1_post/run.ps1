# Divinity: Original Sin EE's replaced passes besides the tone map, Vanilla exact on WARP (tests.cpp): each add-on shader
# compiled with the build's flags against the game's own bytes, and a wrong copy of it that must fail. The game's bytes
# stay out of the repo: -Census names the d1_ census's cso folder (written from Data\EngineShaders.pak, Materials.pak
# and the exe's run-time HLSL). amd64 (the game is 64-bit).
param([string]$Census = (Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\nontw\divinity1\census\cso'))
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_d1_post'
$testBuild = Join-Path $repairRoot 'build\tw_d1_post_test_amd64'
$shaders = Join-Path $repairRoot 'src\games\divinityoriginalsinenhanced\shaders'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
if (-not (Test-Path -LiteralPath $Census)) { throw "The census's shaders are not at $Census (-Census)" }

# post: after the tone map (UNORM targets in the game), pre: before it (float targets).
$passes = [ordered]@{
  '88B0D842' = 'pre'; '940951FA' = 'pre'; 'ED93F883' = 'pre'; '93AEBB93' = 'pre'; 'D39E39C8' = 'pre'; '2F56D195' = 'pre'
  '23B84721' = 'pre'; '7CBAA94E' = 'pre'; '25539638' = 'pre'; 'AD516F39' = 'pre'; '47E69078' = 'pre'
  '2400D20A' = 'post'; '35F39F1D' = 'post'; 'B7FC46C3' = 'post'; '6AF33A9B' = 'post'; 'FB839D33' = 'post'; 'DDBCD866' = 'post'
  '9B114FEF' = 'post'; '83553BB1' = 'post'; '20163A55' = 'post'; 'CB4B3BA6' = 'post'; '8F114E47' = 'post'
}
# Point and spot lights, plain and shadowed: the light rows.
$lightRows = @('D39E39C8', '2F56D195', '23B84721', '7CBAA94E')
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
  # The light passes: Player / Local Light Intensity at 2 must double the game's light.
  $row = if ($lightRows -contains $hash) { ' row' } else { '' }
  # The PCSS sun shadows: R1 save T1's traced b0 / b12 (pcss_r1_cb0.txt: its 64 traced floats, then an orthographic
  # projection of the two cascades' widths and far planes, which the trace does not hold).
  if ($hash -eq '8F114E47') { $row += " cb0=$testRoot\pcss_r1_cb0.txt cb12=$testRoot\pcss_r1_cb12.txt" }
  $manifest += "$hash $($passes[$hash]) $game $addon $mutant$row"
}
$manifestPath = Join-Path $testBuild 'manifest.txt'
Set-Content -LiteralPath $manifestPath -Value $manifest -Encoding ascii

$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 /I $repairRoot "$testRoot\tests.cpp" /Fe:d1_post.exe /link /OPT:REF /OPT:ICF d3d11.lib d3dcompiler.lib dxguid.lib
if ($LASTEXITCODE -ne 0) { throw 'Divinity 1 post passes test compile failed' }
& (Join-Path $testBuild 'd1_post.exe') $manifestPath
if ($LASTEXITCODE -ne 0) { throw 'Divinity 1 post passes test failed' }
