# PsychoV35 and Hue Clip (Attila's Rendering Mode 3, WH3's 4) on WARP (tests.cpp) with the tone mappers compiled from this
# tree and from -Before with the build's flags: Attila's t00 - t10 and WH3's t00. Every other mode must give the same bits
# as -Before; Hue Clip and PsychoV35 are checked on their own (tests.cpp's header). -Before is 4b509903 (hdr audit R4:
# BT.709 on Output SDR in V30 / V31, the newest change those modes were meant to take); against d3d6425b, the commit
# before PsychoV35, only Output SDR in Attila's V30 and WH3's V30 / V31 differ (2026-11-16: 30604 pixels, all there).
# Part 9's control is -Before's PsychoV35 (before its edge-input fix, 2026-11-17): keep -Before older than that fix.
# -AllShaders also compiles every shader of both add-ons on both sides and lists the ones whose bytes changed; -ReportOnly
# runs part 4 alone. R.U.S.E.'s post (parts 3, 4, 6) is compiled from this tree alone, as ps_5_0 /Gec; its 25 D3D9
# replacements are compiled as ps_3_0 with their instruction slots listed.
# -OutDir: where the test exe is built and run (ATN's antivirus-excluded test folder, tools\tw\testbin); else build\.
# Run as x86 and amd64.
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'x86', [switch]$AllShaders, [switch]$ReportOnly, [string]$Before = '4b5099035',
      [string]$OutDir = '', [string]$GammaBefore = '242d6101')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_psychov35'
$testBuild = if ($OutDir) { Join-Path $OutDir "tw_psychov35_$Architecture" } else { Join-Path $repairRoot "build\tw_psychov35_test_$Architecture" }
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null

# The compilers the build found (build32's, else build's).
$cache = Join-Path $repairRoot 'build32\CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cache)) { $cache = Join-Path $repairRoot 'build\CMakeCache.txt' }
$fxc = (Select-String -LiteralPath $cache -Pattern '^FXC_BIN:FILEPATH=(.*)$').Matches[0].Groups[1].Value
$dxc = (Select-String -LiteralPath $cache -Pattern '^DXC_BIN:FILEPATH=(.*)$').Matches[0].Groups[1].Value
$beforeTree = Join-Path $testBuild 'before'
if (Test-Path -LiteralPath $beforeTree) { Remove-Item -LiteralPath $beforeTree -Recurse -Force -Confirm:$false }
New-Item -ItemType Directory -Force -Path $beforeTree | Out-Null
$archive = Join-Path $testBuild 'before.tar'
# Both add-ons and every folder their shaders include from.
& git -C $repairRoot archive --output=$archive $Before src/games/twattila src/games/twwarhammer3 src/games/twrome2 `
    src/games/twwarhammer2 src/games/twshogun2completeedition src/games/twthronesofbritannia src/games/talesofarise_dlss `
    src/shaders src/mods src/utils
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
  # Rome II's chain: part 4's report only (no bit comparison).
  'r_t00' = 'twrome2\shaders\t00_0xEE1DA45C.ps_4_0.hlsl'; 'r_t01' = 'twrome2\shaders\t01_0x850AEE44.ps_4_0.hlsl'
  # Thrones of Britannia's chain: parts 4 and 8's reports only.
  'th_t00' = 'twthronesofbritannia\shaders\t00_0x8BF6B657.ps_4_0.hlsl'; 'th_t01' = 'twthronesofbritannia\shaders\t01_0xD0FAF089.ps_4_0.hlsl'
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

# Part 7 (Gamma Correction in every mode): Rome II's, Warhammer I's / II's, Troy's and Pharaoh's hdr_to_screen t01 and
# Warhammer III's t00 from this tree and from $GammaBefore (the commit before it acted in their HDR modes).
$gammaTree = Join-Path $testBuild 'gamma_before'
if (Test-Path -LiteralPath $gammaTree) { Remove-Item -LiteralPath $gammaTree -Recurse -Force -Confirm:$false }
New-Item -ItemType Directory -Force -Path $gammaTree | Out-Null
$gammaArchive = Join-Path $testBuild 'gamma_before.tar'
& git -C $repairRoot archive --output=$gammaArchive $GammaBefore src/games/twattila src/games/twrome2 src/games/twwarhammer `
    src/games/twwarhammer2 src/games/twwarhammer3 src/games/twtroy src/games/twpharaohdynasties src/shaders src/mods src/utils
if ($LASTEXITCODE -ne 0) { throw 'git archive failed' }
& (Join-Path $env:SystemRoot 'System32\tar.exe') -xf $gammaArchive -C $gammaTree
if ($LASTEXITCODE -ne 0) { throw 'tar failed' }
$gammaShaders = [ordered]@{
  'g_rome2' = 'twrome2\shaders\t01_0x850AEE44.ps_4_0.hlsl'; 'g_wh1' = 'twwarhammer\shaders\t01_0x10759AE3.ps_4_0.hlsl'
  'g_wh2' = 'twwarhammer2\shaders\t01_0x187C9C6D.ps_5_0.hlsl'; 'g_troy' = 'twtroy\shaders\t01_0x48DBD700.ps_5_0.hlsl'
  'g_pharaoh' = 'twpharaohdynasties\shaders\t01_0x769869A4.ps_5_0.hlsl'; 'g_wh3' = 'twwarhammer3\shaders\t00_0xE2EE82F8.ps_5_0.hlsl'
}
foreach ($name in $gammaShaders.Keys) {
  Compile-Shader (Join-Path $gammaTree "src\games\$($gammaShaders[$name])") (Join-Path $beforeShaders "$name.cso") $false
  Compile-Shader (Join-Path $repairRoot "src\games\$($gammaShaders[$name])") (Join-Path $afterShaders "$name.cso") $false
}

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

# Hue Clip alone (twattila/psycho35.hlsli, the one copy every game includes).
# From here on fxc's and clang's warnings on stderr must not stop the script: exit codes decide.
$ErrorActionPreference = 'Continue'
foreach ($game in @('twattila')) {
  & $fxc (Join-Path $testRoot 'hue_clip.hlsl') /T cs_5_0 /D __SHADER_TARGET_MAJOR=5 /D __SHADER_TARGET_MINOR=0 /DNDEBUG /Gec /nologo /O3 /E main `
      /I (Join-Path $repairRoot "src\games\$game") /Fo (Join-Path $afterShaders "hue_clip_$game.cso") 2>$null | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "fxc failed: hue_clip.hlsl ($game)" }
}
# Part 9's PsychoV35 alone (v35_edges.hlsl), with this tree's Attila folder and -Before's (the control).
foreach ($side in @(@{ Root = $repairRoot; Out = $afterShaders }, @{ Root = $beforeTree; Out = $beforeShaders })) {
  & $fxc (Join-Path $testRoot 'v35_edges.hlsl') /T cs_5_0 /D __SHADER_TARGET_MAJOR=5 /D __SHADER_TARGET_MINOR=0 /DNDEBUG /Gec /nologo /O3 /E main `
      /I (Join-Path $side.Root 'src\games\twattila') /Fo (Join-Path $side.Out 'v35_edges.cso') 2>$null | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "fxc failed: v35_edges.hlsl ($($side.Root))" }
}
# Part 5's curves (hue_curves.hlsl), with Attila's folder as the include path.
& $fxc (Join-Path $testRoot 'hue_curves.hlsl') /T ps_5_0 /D __SHADER_TARGET_MAJOR=5 /D __SHADER_TARGET_MINOR=0 /DNDEBUG /Gec /nologo /O3 /E main `
    /I (Join-Path $repairRoot 'src\games\twattila') /Fo (Join-Path $afterShaders 'hue_curves.cso') 2>$null | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'fxc failed: hue_curves.hlsl' }
# R.U.S.E.'s post as ps_5_0 (the build's flags; /Gec takes its D3D9 sampler2D / COLOR syntax).
& $fxc (Join-Path $repairRoot 'src\games\ruse\shaders\postprocess00_0x232EB5DF.ps_3_0.hlsl') /T ps_5_0 /D __SHADER_TARGET_MAJOR=5 `
    /D __SHADER_TARGET_MINOR=0 /DNDEBUG /Gec /nologo /O3 /E main /Fo (Join-Path $afterShaders 'ruse_post.cso') 2>$null | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'fxc failed: R.U.S.E. post' }
# R.U.S.E.'s replacements as the game runs them (ps_3_0, the build's flags): instruction slots.
$slots = foreach ($file in Get-ChildItem -LiteralPath (Join-Path $repairRoot 'src\games\ruse\shaders') -Filter '*.ps_3_0.hlsl') {
  $listing = Join-Path $testBuild 'ruse_slots.asm'
  & $fxc $file.FullName /T ps_3_0 /D __SHADER_TARGET_MAJOR=3 /D __SHADER_TARGET_MINOR=0 /DNDEBUG /Gec /nologo /O3 /E main `
      /Fc $listing /Fo (Join-Path $testBuild 'ruse_slots.cso') 2>$null | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "fxc failed: $($file.Name)" }
  $count = [int](Select-String -LiteralPath $listing -Pattern 'approximately (\d+) instruction slots used').Matches[0].Groups[1].Value
  "$($file.Name -replace '\.ps_3_0\.hlsl$', '') $count"
}
# ps_3_0 guarantees 512 slots; the posts need the caps' 32768 (every D3D9 GPU of the last decade), as v1.00's (1,635) did.
Write-Host "6. R.U.S.E. ps_3_0 instruction slots: $($slots -join ', ')"

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
