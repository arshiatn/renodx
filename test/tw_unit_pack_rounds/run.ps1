# Attila's unit motion over the game's repeated bone packs on WARP (tests.cpp). Needs the Attila addon built in build32 (its
# unit motion shaders' .cso under build32\twattila.include\embed) and the game's shader dump
# (bin\cmd_Decompiler-1.3.16\dump_shader_twattila_pack\cso, not in the repository: the game's 0x5F9CD405.vs_4_0.cso).
# Runs every case twice, the unit records' constants in one buffer and in the motion rings (`in_ring`), each run's motion
# targets dumped (RENODX_TW_MOTION_DUMP), and the two dumps compared byte for byte. Linked large address aware, as
# Attila.exe: on x86 WARP's copies for case 7's 4,550 draws a frame outgrow 2 GB.
#   -OutDir: where the test exe and the dumps go (default build\tw_unit_pack_rounds_test_<arch>).
#   -GameShaders: the game's shader dump (default the repository's bin path above).
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'x86', [string]$OutDir = '', [string]$GameShaders = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_unit_pack_rounds'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
$testBuild = if ($OutDir -ne '') { $OutDir } else { Join-Path $repairRoot "build\tw_unit_pack_rounds_test_$Architecture" }
$gameShaders = if ($GameShaders -ne '') { $GameShaders } else { Join-Path $repairRoot 'bin\cmd_Decompiler-1.3.16\dump_shader_twattila_pack\cso' }
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $repairRoot /I $testRoot /I "$repairRoot\external\reshade" /I "$repairRoot\external\frozen\include" /I "$repairRoot\external\Detours\include" /I "$repairRoot\external\gtl\include" "$testRoot\tests.cpp" /Fe:unit_pack_rounds.exe /link /OPT:REF /OPT:ICF /LARGEADDRESSAWARE user32.lib d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) { throw 'Unit pack rounds test compile failed' }
$dumps = @()
foreach ($mode in @('one_buffer', 'in_ring')) {
  $dump = Join-Path $testBuild "motion_dump_$mode.bin"
  $env:RENODX_TW_MOTION_DUMP = $dump
  $arguments = @((Join-Path $repairRoot 'build32\twattila.include\embed'), $gameShaders)
  if ($mode -eq 'in_ring') { $arguments += 'in_ring' }
  & (Join-Path $testBuild 'unit_pack_rounds.exe') @arguments
  if ($LASTEXITCODE -ne 0) { throw "Unit pack rounds test failed ($mode)" }
  $dumps += $dump
}
Remove-Item Env:\RENODX_TW_MOTION_DUMP
$one = [System.IO.File]::ReadAllBytes($dumps[0])
$ring = [System.IO.File]::ReadAllBytes($dumps[1])
"motion dumps: $($one.Length) and $($ring.Length) bytes"
if ($one.Length -eq 0 -or (Get-FileHash -LiteralPath $dumps[0]).Hash -ne (Get-FileHash -LiteralPath $dumps[1]).Hash) {
  $first = 0
  while ($first -lt [Math]::Min($one.Length, $ring.Length) -and $one[$first] -eq $ring[$first]) { ++$first }
  throw "The motion dumps differ (one buffer vs in the rings), first at byte $first"
}
'PASS: the motion dumps byte-equal'
