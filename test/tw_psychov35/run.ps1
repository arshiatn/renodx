# PsychoV35 and Hue Clip (Attila's Rendering Mode 3, WH3's 4) on WARP (tests.cpp) with the tone mappers compiled from this
# tree and from d3d6425b (the commit before them) with the build's flags: Attila's t00 - t10 and WH3's t00. Every other
# mode must give the same bits as d3d6425b; Hue Clip and PsychoV35 are checked on their own (tests.cpp's header).
# -AllShaders also compiles every shader of both add-ons on both sides and lists the ones whose bytes changed; -ReportOnly
# runs part 4 alone.
# Run as x86 and amd64.
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'x86', [switch]$AllShaders, [switch]$ReportOnly, [string]$Before = 'd3d6425b')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_psychov35'
$testBuild = Join-Path $repairRoot "build\tw_psychov35_test_$Architecture"
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null

$fxc = (Select-String -LiteralPath (Join-Path $repairRoot 'build32\CMakeCache.txt') -Pattern '^FXC_BIN:FILEPATH=(.*)$').Matches[0].Groups[1].Value
$dxc = (Select-String -LiteralPath (Join-Path $repairRoot 'build32\CMakeCache.txt') -Pattern '^DXC_BIN:FILEPATH=(.*)$').Matches[0].Groups[1].Value
$beforeTree = Join-Path $testBuild 'before'
if (Test-Path -LiteralPath $beforeTree) { Remove-Item -LiteralPath $beforeTree -Recurse -Force -Confirm:$false }
New-Item -ItemType Directory -Force -Path $beforeTree | Out-Null
$archive = Join-Path $testBuild 'before.tar'
# Both add-ons and every folder their shaders include from.
& git -C $repairRoot archive --output=$archive $Before src/games/twattila src/games/twwarhammer3 src/games/twrome2 `
    src/games/twwarhammer2 src/games/twshogun2completeedition src/games/talesofarise_dlss src/shaders src/mods src/utils
if ($LASTEXITCODE -ne 0) { throw 'git archive failed' }
& (Join-Path $env:SystemRoot 'System32\tar.exe') -xf $archive -C $beforeTree
if ($LASTEXITCODE -ne 0) { throw 'tar failed' }

# The build's flags (CMakeLists.txt): fxc with the target's major / minor, NDEBUG, /Gec, /O3; /Qstrip_reflect only for the
# byte list (tests.cpp binds by the reflection's names; the code is the same).
function Compile-Shader([string]$Source, [string]$Output, [bool]$Strip) {
  $target = [IO.Path]::GetFileNameWithoutExtension($Source) -replace '^.*\.', ''
  $match = [regex]::Match($target, '^(\w+)_(\d)_(\d|x)$')
  $minor = if ($match.Groups[3].Value -eq 'x') { '0' } else { $match.Groups[3].Value }
  $profile = "$($match.Groups[1].Value)_$($match.Groups[2].Value)_$minor"
  $compiler = $fxc
  if ([int]$match.Groups[2].Value -ge 6) {
    # Shader model 6 (the DLAA helper's): dxc with the build's flags.
    $compiler = $dxc
    $arguments = @($Source, '-T', $profile, '/HV', '2021', '/DNDEBUG', '/nologo', '/O3', '-E', 'main', '-Fo', $Output)
    if ([int]$minor -ge 2) { $arguments += '/enable-16bit-types' }
  } else {
    $arguments = @($Source, '/T', $profile, '/D', "__SHADER_TARGET_MAJOR=$($match.Groups[2].Value)", '/D', "__SHADER_TARGET_MINOR=$minor",
                   '/DNDEBUG', '/Gec', '/nologo', '/O3', '/E', 'main', '/Fo', $Output)
  }
  if ($Strip) { $arguments += '/Qstrip_reflect' }
  # The compilers' warnings go to stderr, which Windows PowerShell would turn into errors here.
  $ErrorActionPreference = 'Continue'
  & $compiler @arguments 2>$null | Out-Null
  $ErrorActionPreference = 'Stop'
  if ($LASTEXITCODE -ne 0) { throw "Compile failed: $Source" }
}

$toneMappers = [ordered]@{
  'a_t00' = 'twattila\shaders\t00_0x8BF6B657.ps_4_0.hlsl'; 'a_t01' = 'twattila\shaders\t01_0x2C8169CA.ps_4_0.hlsl'
  'a_t02' = 'twattila\shaders\t02_0xFEA9F9E2.ps_4_0.hlsl'; 'a_t03' = 'twattila\shaders\t03_0xBA446301.ps_4_0.hlsl'
  'a_t04' = 'twattila\shaders\t04_0xB7FBF3E2.ps_4_0.hlsl'; 'a_t05' = 'twattila\shaders\t05_0xA2D88FCD.ps_5_0.hlsl'
  'a_t06' = 'twattila\shaders\t06_0xCC79C76F.ps_5_0.hlsl'; 'a_t07' = 'twattila\shaders\t07_0x836B7901.ps_5_0.hlsl'
  'a_t08' = 'twattila\shaders\t08_0xF42FDDC6.ps_5_0.hlsl'; 'a_t09' = 'twattila\shaders\t09_0x95398068.ps_4_0.hlsl'
  'a_t10' = 'twattila\shaders\t10_0x6E20B0EF.ps_4_0.hlsl'; 'w_t00' = 'twwarhammer3\shaders\t00_0xE2EE82F8.ps_5_0.hlsl'
}
$beforeShaders = Join-Path $testBuild 'before_cso'
$afterShaders = Join-Path $testBuild 'after_cso'
New-Item -ItemType Directory -Force -Path $beforeShaders, $afterShaders | Out-Null
$changed = @()
foreach ($name in $toneMappers.Keys) {
  foreach ($side in @(@{ Root = (Join-Path $beforeTree 'src\games'); Out = $beforeShaders }, @{ Root = (Join-Path $repairRoot 'src\games'); Out = $afterShaders })) {
    $source = Join-Path $side.Root $toneMappers[$name]
    Compile-Shader $source (Join-Path $side.Out "$name.cso") $false
    Compile-Shader $source (Join-Path $side.Out "$name.stripped.cso") $true
  }
  $a = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $beforeShaders "$name.stripped.cso")).Hash
  $b = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $afterShaders "$name.stripped.cso")).Hash
  if ($a -ne $b) { $changed += $name }
}
Write-Host "Tone mappers whose bytes changed since ${Before}: $($changed -join ', ') (WARP compares their output)"

if ($AllShaders) {
  # Every other shader of both add-ons: the same bytes on both sides (the tone mappers' list above aside).
  $others = @()
  foreach ($game in @('twattila', 'twwarhammer3')) {
    $sources = Get-ChildItem -LiteralPath (Join-Path $repairRoot "src\games\$game") -Recurse -Filter '*.hlsl' |
               Where-Object { $_.Name -match '\.(ps|vs|cs|gs|hs|ds|lib)_\d_(\d|x)\.hlsl$' }
    foreach ($file in $sources) {
      $relative = $file.FullName.Substring((Join-Path $repairRoot 'src\games').Length + 1)
      if ($toneMappers.Values -contains $relative) { continue }
      $beforeSource = Join-Path (Join-Path $beforeTree 'src\games') $relative
      if (-not (Test-Path -LiteralPath $beforeSource)) { $others += "$relative (new)"; continue }
      $outA = Join-Path $testBuild 'all_a.cso'
      $outB = Join-Path $testBuild 'all_b.cso'
      Compile-Shader $beforeSource $outA $true
      Compile-Shader $file.FullName $outB $true
      if ((Get-FileHash -LiteralPath $outA).Hash -ne (Get-FileHash -LiteralPath $outB).Hash) { $others += $relative }
    }
  }
  Write-Host "Other shaders whose bytes changed: $(if ($others.Count) { $others -join ', ' } else { 'none' })"
  if ($others.Count -ne 0) { throw 'Shaders outside the tone mappers changed' }
}

# Hue Clip alone, from each game's copy of psycho_test31.hlsli.
# From here on fxc's and clang's warnings on stderr must not stop the script: exit codes decide.
$ErrorActionPreference = 'Continue'
foreach ($game in @('twattila', 'twwarhammer3')) {
  & $fxc (Join-Path $testRoot 'hue_clip.hlsl') /T cs_5_0 /D __SHADER_TARGET_MAJOR=5 /D __SHADER_TARGET_MINOR=0 /DNDEBUG /Gec /nologo /O3 /E main `
      /I (Join-Path $repairRoot "src\games\$game") /Fo (Join-Path $afterShaders "hue_clip_$game.cso") 2>$null | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "fxc failed: hue_clip.hlsl ($game)" }
}

$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 -Wno-macro-redefined `
    /I $repairRoot "$testRoot\tests.cpp" /Fe:psychov35.exe /link /OPT:REF /OPT:ICF d3d11.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) { throw 'PsychoV35 test compile failed' }
$arguments = @($beforeShaders, $afterShaders)
if ($ReportOnly) { $arguments += '--report' }
& (Join-Path $testBuild 'psychov35.exe') @arguments
if ($LASTEXITCODE -ne 0) { throw 'PsychoV35 test failed' }
