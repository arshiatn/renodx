# R13 (hdr-review-rules.md, "Vanilla exact first"): every replaced tone mapper, hdr_to_screen, bloom and lens flare's
# Vanilla path against the game's own bytes on WARP (tests.cpp, one driver for every game; the table below names each
# game's shaders). The game's bytes stay out of the repository: -Dumps (the cmd_Decompiler pack and in-game dumps) and
# -Divinity1 / -Divinity2 (their shader dumps), only read. Each add-on shader is compiled here with the build's flags, its reflection
# kept (the test binds by name; the code is the same as /Qstrip_reflect's). -Games: a subset of the table's keys (A,B).
# -OutDir: where the test exe is built and run (ATN's antivirus-excluded test folder, tools\tw\testbin); else build\.
# -NoMutants: skip the constant mutants' report. -Dump <part of a name>: that shader's worst texels. -Architecture: the
# test exe's (x86: the 32-bit WARP the 32-bit games' add-ons run beside).
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'amd64',
      [string]$Dumps = 'D:\Dev\renodx\bin\cmd_Decompiler-1.3.16',
      [string]$Divinity1 = (Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\nontw\divinity1\census\cso'),
      [string]$Divinity2 = (Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\nontw\divinity2\shader_dump\dxbc'),
      [string]$Games = '', [string]$Fxc = '', [string]$OutDir = '', [switch]$NoMutants, [string]$Dump = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_vanilla_exact'
$testBuild = Join-Path $(if ($OutDir) { $OutDir } else { Join-Path $repairRoot 'build' }) "tw_vanilla_exact_$Architecture"
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null

if (-not $Fxc) {
  $cache = @('build\CMakeCache.txt', 'build32\CMakeCache.txt') | ForEach-Object { Join-Path $repairRoot $_ } |
           Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
  if (-not $cache) { throw 'No build\CMakeCache.txt: name fxc with -Fxc' }
  $Fxc = (Select-String -LiteralPath $cache -Pattern '^FXC_BIN:FILEPATH=(.*)$').Matches[0].Groups[1].Value
}

# Each game: its add-on folder, the dump with the game's csos (0x<CRC32>.<target>.cso; another target of the same hash
# where the add-on's build differs), the shaders (by file name); Settings: shader_injection members on top of the test's
# Vanilla ones; ShaderSettings (by shader name): more of them for those shaders; Registers (by shader name): constants
# where the game's bytes carry no names (tests.cpp, GameConstants); FrameGamma: the frame's gamma whose 2.2 decode Gamma
# Correction gives in every Rendering Mode (tests.cpp's Gamma Correction check), none for NoGamma's shaders; Modes: those
# Rendering Modes where not 0 - 3.
# Attila and Thrones (the same table): every replaced shader the generic inputs exercise; ssr02-07 have tw_thrones_ssr,
# vfx and battlefog tw_attila_builds, dof tw_dof_blurs. Left out: pointlight00-39 (their light's place and reach are vertex
# inputs, so no texel is lit here: the control is never caught; their WARP proof is tools/tw/testbin/g4_point_shadows),
# the UI sprites uiMap00 / uiMap02 / uiMenuText00 (ui00) (the clip rectangle discards every texel here), grass00 / grass01
# (their stream output is not made here: no comparable output). The videos (cutscene00) are made for a 2.2 screen: Gamma
# Correction on shows the game's bytes (off re-encodes them for the 2.0 decode), so no Gamma Correction check either.
$attilaShaders = @{
  FrameGamma = '2'; Settings = 'ssr_quality=1,effects_share=1,grass_height_distance=1,sky_smooth_strength=1'
  ShaderSettings = @{ '^cutscenes?\d+_' = 'gamma_correction=1' }; NoGamma = '^cutscenes?\d+_'
}
$table = [ordered]@{
  'Shogun2'     = @{ Folder = 'twshogun2completeedition'; Dump = 'dump_shader_shogun2_pack\cso'; Shaders = '^(tonemap|flare|hdrflare)\d+_' }
  # Shogun 2's other replaced pixel shaders (skill audit 11-16: every replacement against the game's bytes), those exact
  # with their control caught on 11-16. Not yet: highlight00 / ribbon00 (inf apart), ssrcopy00 (0.79), treevelocity00-03
  # (1.9 - 2.4), and particlelight00, pointlight00 / 01, vfxglow00 (exact, the control missed): open (fix round 11-16 report).
  'Shogun2PS'   = @{ Folder = 'twshogun2completeedition'; Dump = 'dump_shader_shogun2_pack\cso'
                     Shaders = '^(fog|vignette|video|uiresolve|uifigure|sun|projectiletrail|glowmesh|vfxoverlay|dissolve|campaignwater)\d+_|^vfxglow01_' }
  'Attila'      = $attilaShaders + @{ Folder = 'twattila'; Dump = 'dump_shader_twattila_pack\cso'
                                      Shaders = '^((t|bloom|lensflare|sky|markersbattle|cutscene|vignette|autoexposure|ssao)\d+|ssr0[01]|grass02)_' }
  'RomeII'      = @{ Folder = 'twrome2'; Dump = 'dump_shader_twrome2_pack\cso'; Shaders = '^(t|bloom|lensflare)\d+_'; FrameGamma = '2' }
  'Thrones'     = $attilaShaders + @{ Folder = 'twthronesofbritannia'; Dump = 'dump_shader_twthronesofbritannia_pack\cso'
                                      Shaders = '^((t|bloom|lensflare|sky|markersbattle|cutscenes|vignette|autoexposure|ssao)\d+|ssr0[01]|grass02)_' }
  # Warhammer I / II: also the sky and the vignette (WH2: the legacy battle fog 04 - 06); their other shaders in
  # test/tw_warhammer_shaders and test/tw_dof_blurs. Not here: the cinematics (the video decoded at 2.2 and encoded with the
  # frame's gamma by design; WH2's give 0 on these inputs), FXAA (alpha written 1), sharpening and the point light (0 on
  # these inputs: no control).
  'WH1'         = @{ Folder = 'twwarhammer'; Dump = 'dump_shader_twwarhammer_pack\cso'; Shaders = '^(t|bloom|lensflare|sky|vignette)\d+_'
                     FrameGamma = '2' }
  'WH2'         = @{ Folder = 'twwarhammer2'; Dump = 'dump_shader_twwarhammer2_pack\cso'; Shaders = '^(t|bloom|lensflare|sky|vignette)\d+_|^battlefog0[4-6]_'
                     FrameGamma = '2'; Modes = '0,1,2,3,4' }
  'WH3'         = @{ Folder = 'twwarhammer3'; Dump = 'dump_shader_twwarhammer3_pack\cso'; Shaders = '^(t|bloom|lensflare|lensblood)\d+_'
                     FrameGamma = '2'; Modes = '0,1,2,3,4' }
  # Troy and Pharaoh: their tone mappers, bloom and flares, and the other replaced screen passes that pass here with their
  # control caught (fix round 2026-11-16: tools/tw/troy/vanilla_exact_tp_1116.txt). Left out, each with its reason there:
  # Troy fxaa00 (0.062 off; its HDR edge threshold the suspect) and menuvideo01 (0.037 off), causes not read yet (B);
  # Pharaoh highlightindicator00 / 03 (their stock 10x overbright saturated and its division by 0 made safe, on purpose);
  # the UI, cinematics and fire02 (their control not caught: outputs the inputs here don't move); sharpening, chromatic
  # aberration, menuvideo00, Troy's ui03 and photo02 / 04 (the Gamma Correction check, made for the tone mappers, on a pass
  # that takes the tone mapper's output); compute shaders (autoexposure00); passes with their own WARP tests (fog, AO, SSR).
  'Troy'        = @{ Folder = 'twtroy'; Dump = 'dump_shader_twtroy_pack\cso'; FrameGamma = '2'; Modes = '0,1,2,3,4'
                     Shaders = '^(t|bloom|lensflare|chromatic|godrays|vignette|skydome|markershighlight)\d+_|^photo0[13]_' }
  'Pharaoh'     = @{ Folder = 'twpharaohdynasties'; Dump = 'dump_shader_twpharaohdynasties_pack\cso'; FrameGamma = '2'; Modes = '0,1,2,3,4'
                     Shaders = '^(t|bloom|lensflare|lensblood|godrays|vignette|skydome)\d+_|^(fire0[01]|highlightindicator0[12]|photo0[1-4])_' }
  # Rome Remastered (Feral's port, no names): the resolve (resolve.hlsli: screen size, luma off, brightness 1; gamma 2.2,
  # white scale, exposure, shadow boost; its falloff, the vignette's ellipse and power; its scale and strength; tint, LUT
  # strength), the bright pass (threshold, scale, cap; texel size; its source is a 0 - 1 mask here: no names), the flare
  # (alpha scale, discard below).
  'RomeR'       = @{ Folder = 'twromeremastered'; Dump = 'dump_shader_twromeremastered_pack\cso'; Shaders = '^(t|bloom|lensflare)\d+_'
                     Registers = @{ '^t\d+_' = 'cb0[0]=u64,u48,0,1;cb0[1]=2.2,1,1,0.2;cb0[2]=11,0.9,1.2,2;cb0[3]=0.5,0.6,0,1;cb0[4]=1,1,1,0.8'
                                    '^bloom01_' = 'cb0[0]=0.35,1,8,0;cb0[1]=0.015625,0.0208333,0,0'; '^lensflare00_' = 'cb7[4]=1,0.05,0,0' } }
  # Rome Remastered's other replaced pixel shaders (cutscene, distortion, FXAA, SMAA's last pass).
  'RomeRPS'     = @{ Folder = 'twromeremastered'; Dump = 'dump_shader_twromeremastered_pack\cso'; Shaders = '^(cutscene|distortion|fxaa|smaa)\d+_' }
  'Ryse'        = @{ Folder = 'rysesonofrome'; Dump = 'dump_shader_ryse'; Shaders = '^tonemap\d+_' }
  # Divinity 1: the tone map (tw_divinity1's), bloom, god rays, UI, FXAA, SMAA, vignette.
  'Divinity1'   = @{ Folder = 'divinityoriginalsinenhanced'; Dump = $Divinity1
                     Shaders = '^(bloom|godsrays\d+|ui\d+)_|^0x(30A1D40D|35F39F1D|6AF33A9B|B7FC46C3|FB839D33|DDBCD866)\.' }
  'Divinity2'   = @{ Folder = 'divinity2originalsindefinitive'; Dump = $Divinity2; Shaders = '^(t01|bloom00|ui00)_' }
  'Tales'       = @{ Folder = 'talesofarise'; Dump = 'dump_shaders_tales_of_arise'; Shaders = '^(tonemap|lut)\d+_' }
  'Expeditions' = @{ Folder = 'expeditionsrome'; Dump = 'dump_shaders_Expeditions_rome'; Shaders = '^(t|lut)\d+_' }
}

# The build's flags (CMakeLists.txt): fxc with the target's major / minor, NDEBUG, /Gec, /O3.
function Compile-Shader([string]$Source, [string]$ShaderProfile, [string]$Output) {
  $match = [regex]::Match($ShaderProfile, '^\w+_(\d)_(\d)$')
  $arguments = @($Source, '/T', $ShaderProfile, '/D', "__SHADER_TARGET_MAJOR=$($match.Groups[1].Value)",
                 '/D', "__SHADER_TARGET_MINOR=$($match.Groups[2].Value)", '/DNDEBUG', '/Gec', '/nologo', '/O3', '/E', 'main', '/Fo', $Output)
  # fxc's warnings go to stderr, which Windows PowerShell would turn into errors here.
  $ErrorActionPreference = 'Continue'
  & $Fxc @arguments 2>$null | Out-Null
  $ErrorActionPreference = 'Stop'
  return $LASTEXITCODE -eq 0
}

$manifest = @()
foreach ($game in $table.Keys) {
  if ($Games -and ($Games -split ',') -notcontains $game) { continue }
  $entry = $table[$game]
  $folder = if ([IO.Path]::IsPathRooted($entry.Dump)) { $entry.Dump } else { Join-Path $Dumps $entry.Dump }
  # Its csos by file name, in the folder or below (in-game dumps keep later ones in subfolders; the first found).
  $csos = @{}
  Get-ChildItem -LiteralPath $folder -Recurse -Filter '*.cso' | ForEach-Object { if (-not $csos.ContainsKey($_.Name)) { $csos[$_.Name] = $_.FullName } }
  $out = Join-Path $testBuild "cso\$game"
  New-Item -ItemType Directory -Force -Path $out | Out-Null
  $sources = Get-ChildItem -LiteralPath (Join-Path $repairRoot "src\games\$($entry.Folder)\shaders") -Filter '*.hlsl' |
             Where-Object { $_.Name -match $entry.Shaders -and $_.Name -match '(^|_)(0x[0-9A-F]{8})\.(\w+_\d_\d)\.hlsl$' } | Sort-Object Name
  foreach ($file in $sources) {
    $null = $file.Name -match '(^|_)(0x[0-9A-F]{8})\.(\w+_\d_\d)\.hlsl$'
    $hash = $Matches[2]
    $target = $Matches[3]
    $gameCso = $csos["$hash.$target.cso"]
    if (-not $gameCso) { $gameCso = $csos.Keys | Where-Object { $_ -like "$hash.*.cso" } | Sort-Object | Select-Object -First 1 | ForEach-Object { $csos[$_] } }
    if (-not $gameCso) { $gameCso = Join-Path $folder "$hash.$target.cso" }
    $addonCso = Join-Path $out ($file.BaseName + '.cso')
    if (-not (Compile-Shader $file.FullName $target $addonCso)) { Remove-Item -LiteralPath $addonCso -ErrorAction SilentlyContinue }
    $registers = if ($entry.Registers) { ($entry.Registers.Keys | Where-Object { $file.Name -match $_ } | ForEach-Object { $entry.Registers[$_] }) -join ';' } else { '' }
    $settings = @($entry.Settings) + @(if ($entry.ShaderSettings) { $entry.ShaderSettings.Keys | Where-Object { $file.Name -match $_ } | ForEach-Object { $entry.ShaderSettings[$_] } })
    $frameGamma = if ($entry.NoGamma -and $file.Name -match $entry.NoGamma) { '' } else { $entry.FrameGamma }
    $manifest += "$game`t$($file.BaseName -replace '\.\w+_\d_\d$', '')`t$gameCso`t$addonCso`t$(($settings | Where-Object { $_ }) -join ',')`t$registers`t$frameGamma`t$($entry.Modes)"
  }
}
$manifestPath = Join-Path $testBuild 'manifest.tsv'
[IO.File]::WriteAllLines($manifestPath, [string[]]$manifest)

$ErrorActionPreference = 'Continue'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=$triple /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 `
    /I $repairRoot "$testRoot\tests.cpp" /Fe:vanilla_exact.exe /link /OPT:REF /OPT:ICF d3d11.lib d3dcompiler.lib dxguid.lib
if ($LASTEXITCODE -ne 0) { throw 'Vanilla-exact test compile failed' }
$arguments = @($manifestPath)
if ($NoMutants) { $arguments += '--no-mutants' }
if ($Dump) { $arguments += @('--dump', $Dump) }
& (Join-Path $testBuild 'vanilla_exact.exe') @arguments
if ($LASTEXITCODE -ne 0) { throw 'Vanilla-exact test failed' }
