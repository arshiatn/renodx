# The depth of field blurs of Attila, Thrones and WARHAMMER (twattila/dof_blur_ps.hlsli, dof_blur_cs.hlsli; each game's
# shaders/dof00-03) on WARP against the game's own bytes (tests.cpp). -GameShaders: the folder holding the games' shader
# dumps (dump_shader_<game>_pack\cso\0x<hash>.<target>.cso), only read. Ours are compiled here with the build's fxc
# flags; the mutant is ours with the SDR tap not clipped at 1 (DofTap's saturate -> max(0)), built from copies in the
# build folder. CPU only (WARP).
param([string]$GameShaders = 'D:\Dev\renodx\bin\cmd_Decompiler-1.3.16', [ValidateSet('x86', 'amd64')][string]$Architecture = 'amd64',
      [string]$BuildDir = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
if ($BuildDir -eq '') { $BuildDir = Join-Path $repairRoot "build\tw_dof_blurs_test_$Architecture" }
$testRoot = Join-Path $repairRoot 'test\tw_dof_blurs'
$games = Join-Path $repairRoot 'src\games'
$mutantRoot = Join-Path $BuildDir 'mutant'
New-Item -ItemType Directory -Force -Path $mutantRoot | Out-Null

$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $BuildDir
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 "$testRoot\tests.cpp" /Fe:dof_blurs.exe /link /OPT:REF /OPT:ICF d3d11.lib dxgi.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) { throw 'DOF blur test compile failed' }

# The mutant's copies: dof_blur.hlsli with the SDR clip at 1 gone, the bodies beside it (they include ./dof_blur.hlsli).
$tap = [IO.File]::ReadAllText((Join-Path $games 'twattila\dof_blur.hlsli'))
if (-not $tap.Contains(': saturate(color);')) { throw 'dof_blur.hlsli changed: update run.ps1' }
[IO.File]::WriteAllText((Join-Path $mutantRoot 'dof_blur.hlsli'), $tap.Replace(': saturate(color);', ': max(color, 0.f);'))
foreach ($body in 'dof_blur_ps.hlsli', 'dof_blur_cs.hlsli') { Copy-Item -LiteralPath (Join-Path $games "twattila\$body") -Destination $mutantRoot -Force }

$fxc = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe'
function Compile([string]$Source, [string]$Out) {
  if ([IO.Path]::GetFileName($Source) -notmatch '\.([pc]s)_([0-9])_([0-9])\.hlsl$') { throw "no target in $Source" }
  $flags = @('/D', "__SHADER_TARGET_MAJOR=$($Matches[2])", '/D', "__SHADER_TARGET_MINOR=$($Matches[3])", '/DNDEBUG', '/Gec')
  # Its warnings go to stderr: not errors here (the exit code is).
  $ErrorActionPreference = 'Continue'
  & $fxc $Source -T "$($Matches[1])_$($Matches[2])_$($Matches[3])" @flags /nologo /O3 /Qstrip_reflect -E main -Fo $Out 2>$null | Out-Null
  $ErrorActionPreference = 'Stop'
  if ($LASTEXITCODE -ne 0) { throw "fxc failed: $Source" }
}

$cases = @()
$packs = @{ twattila = 'twattila'; twthronesofbritannia = 'twthronesofbritannia'; twwarhammer = 'twwarhammer' }
foreach ($game in 'twattila', 'twthronesofbritannia', 'twwarhammer') {
  foreach ($file in Get-ChildItem -LiteralPath (Join-Path $games "$game\shaders") -Filter 'dof0*_0x*.hlsl') {
    if ($file.Name -notmatch '_(0x[0-9A-F]{8})\.((ps|cs)_\d_\d)\.hlsl$') { continue }
    $hash = $Matches[1]
    $target = $Matches[2]
    $text = [IO.File]::ReadAllText($file.FullName)
    if ($text -notmatch 'DofBlur(Horizontal|Vertical|Cs)\([^)]*(true|false)\)') { continue }
    $kind = switch ($Matches[1]) { 'Horizontal' { 'ps_h' } 'Vertical' { 'ps_v' } default { if ($Matches[2] -eq 'true') { 'cs_v' } else { 'cs_h' } } }
    $label = "$game/$($file.Name)"
    $ours = Join-Path $BuildDir "$game`_$hash.cso"
    Compile $file.FullName $ours
    $shared = ((Join-Path $games "$game\shared.h") -replace '\\', '/')
    $mutantText = $text.Replace('#include "../shared.h"', "#include `"$shared`"")
    $mutantText = [regex]::Replace($mutantText, '#include "[^"]*(dof_blur_(ps|cs)\.hlsli)"', { param($m) "#include `"$(($mutantRoot -replace '\\', '/'))/$($m.Groups[1].Value)`"" })
    $mutantSource = Join-Path $mutantRoot "$game`_$($file.Name)"
    [IO.File]::WriteAllText($mutantSource, $mutantText)
    $mutant = Join-Path $BuildDir "$game`_$hash.mutant.cso"
    Compile $mutantSource $mutant
    $gameCso = Join-Path $GameShaders "dump_shader_$($packs[$game])_pack\cso\$hash.$target.cso"
    if (-not (Test-Path -LiteralPath $gameCso)) { throw "no game shader $gameCso" }
    $cases += @($label, $kind, $gameCso, $ours, $mutant)
  }
}
if ($cases.Count -ne 12 * 5) { throw "expected 12 shaders, found $($cases.Count / 5)" }
# Also kept in last_run.txt (a failing run's lines survive a caller's pipeline).
& (Join-Path $BuildDir 'dof_blurs.exe') @cases | Tee-Object -FilePath (Join-Path $BuildDir 'last_run.txt')
if ($LASTEXITCODE -ne 0) { throw 'DOF blur test failed' }
