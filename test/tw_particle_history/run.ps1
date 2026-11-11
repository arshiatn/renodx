# Smooth Half-Size Particles and Attila's Particles at Half Resolution on WARP (tests.cpp), once a game: Warhammer III against
# its addon's shaders (build\twwarhammer3.include\embed) and the game's composite from the shader dump
# (bin\cmd_Decompiler-1.3.16\dump_shader_twwarhammer3_pack\cso, not in the repository: without it the Warhammer III test
# fails), Attila against its addon's (build32\twattila.include\embed). Build both addons first. Run as x86 and amd64;
# -Time also times the passes on the hardware GPU at 4K.
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'x86', [switch]$Time)
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_particle_history'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
$testBuild = Join-Path $repairRoot "build\tw_particle_history_test_$Architecture"
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
$games = @(
  @{ Name = 'wh3'; Define = '/DPARTICLE_TEST_WH3'; Include = "$repairRoot\build\twwarhammer3.include"; Arguments = @("$repairRoot\bin\cmd_Decompiler-1.3.16\dump_shader_twwarhammer3_pack\cso") },
  @{ Name = 'attila'; Define = '/DPARTICLE_TEST_ATTILA'; Include = "$repairRoot\build32\twattila.include"; Arguments = @() }
)
foreach ($game in $games) {
  $exe = "particle_history_$($game.Name).exe"
  & 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations $game.Define /I $repairRoot /I $game.Include /I "$repairRoot\external\reshade" /I "$repairRoot\external\frozen\include" /I "$repairRoot\external\Detours\include" /I "$repairRoot\external\gtl\include" "$testRoot\tests.cpp" /Fe:$exe /link /OPT:REF /OPT:ICF user32.lib d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib
  if ($LASTEXITCODE -ne 0) { throw "Particle history test compile failed ($($game.Name))" }
  $arguments = $game.Arguments
  if ($Time) { $arguments += '--time' }
  & (Join-Path $testBuild $exe) @arguments
  if ($LASTEXITCODE -ne 0) { throw "Particle history test failed ($($game.Name))" }
}
