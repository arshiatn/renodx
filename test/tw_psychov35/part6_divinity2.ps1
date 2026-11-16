# Part 6 (Divinity 2's PsychoV35): its LUT builder t00 and LUT pass t01 on WARP (part6_divinity2.cpp), compiled from this
# tree and from $Before (the commit before PsychoV35) with the build's flags, and part6_divinity2.hlsl (V35 evaluated
# directly). -GameShaders (required): a folder with the game's own 0x7016F292.cs_5_0.cso and 0xC5E7DE4B.ps_5_0.cso (parts
# 5, 8 and 9 compare against them).
# Run as x86 and amd64.
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'amd64', [string]$Before = '3122ccb8',
      [string]$GameShaders = $(throw '-GameShaders: the folder with the game''s own 0x7016F292.cs_5_0.cso and 0xC5E7DE4B.ps_5_0.cso'))
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_psychov35'
$testBuild = Join-Path $repairRoot "build\tw_psychov35_part6_$Architecture"
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null

# fxc from whichever build tree this checkout has (x64 or x86).
$cache = @('build\CMakeCache.txt', 'build32\CMakeCache.txt') | ForEach-Object { Join-Path $repairRoot $_ } |
         Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
$fxc = (Select-String -LiteralPath $cache -Pattern '^FXC_BIN:FILEPATH=(.*)$').Matches[0].Groups[1].Value
$beforeTree = Join-Path $testBuild 'before'
if (Test-Path -LiteralPath $beforeTree) { Remove-Item -LiteralPath $beforeTree -Recurse -Force -Confirm:$false }
New-Item -ItemType Directory -Force -Path $beforeTree | Out-Null
$archive = Join-Path $testBuild 'before.tar'
& git -C $repairRoot archive --output=$archive $Before src/games/divinity2originalsindefinitive src/games/twattila src/shaders
if ($LASTEXITCODE -ne 0) { throw 'git archive failed' }
& (Join-Path $env:SystemRoot 'System32\tar.exe') -xf $archive -C $beforeTree
if ($LASTEXITCODE -ne 0) { throw 'tar failed' }

# The build's flags (CMakeLists.txt): fxc with the target's major / minor, NDEBUG, /Gec, /O3.
function Compile-Shader([string]$Source, [string]$ShaderProfile, [string]$Output, [string]$Include) {
  $match = [regex]::Match($ShaderProfile, '^\w+_(\d)_(\d)$')
  $arguments = @($Source, '/T', $ShaderProfile, '/D', "__SHADER_TARGET_MAJOR=$($match.Groups[1].Value)",
                 '/D', "__SHADER_TARGET_MINOR=$($match.Groups[2].Value)", '/DNDEBUG', '/Gec', '/nologo', '/O3', '/E', 'main', '/Fo', $Output)
  if ($Include) { $arguments += @('/I', $Include) }
  # fxc's warnings go to stderr, which Windows PowerShell would turn into errors here.
  $ErrorActionPreference = 'Continue'
  & $fxc @arguments 2>$null | Out-Null
  $ErrorActionPreference = 'Stop'
  if ($LASTEXITCODE -ne 0) { throw "Compile failed: $Source" }
}

$beforeShaders = Join-Path $testBuild 'before_cso'
$afterShaders = Join-Path $testBuild 'after_cso'
New-Item -ItemType Directory -Force -Path $beforeShaders, $afterShaders | Out-Null
foreach ($side in @(@{ Root = (Join-Path $beforeTree 'src\games'); Out = $beforeShaders }, @{ Root = (Join-Path $repairRoot 'src\games'); Out = $afterShaders })) {
  $folder = Join-Path $side.Root 'divinity2originalsindefinitive\shaders'
  Compile-Shader (Join-Path $folder 't00_0x7016F292.cs_5_0.hlsl') 'cs_5_0' (Join-Path $side.Out 't00.cso') ''
  Compile-Shader (Join-Path $folder 't01_0xC5E7DE4B.ps_5_0.hlsl') 'ps_5_0' (Join-Path $side.Out 't01.cso') ''
}
Compile-Shader (Join-Path $testRoot 'part6_divinity2.hlsl') 'cs_5_0' (Join-Path $afterShaders 'part6_direct.cso') `
    (Join-Path $repairRoot 'src\games\divinity2originalsindefinitive')

$ErrorActionPreference = 'Continue'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 -Wno-macro-redefined `
    /I $repairRoot "$testRoot\part6_divinity2.cpp" /Fe:part6_divinity2.exe /link /OPT:REF /OPT:ICF d3d11.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) { throw 'Part 6 test compile failed' }
& (Join-Path $testBuild 'part6_divinity2.exe') $beforeShaders $afterShaders $GameShaders
if ($LASTEXITCODE -ne 0) { throw 'Part 6 test failed' }
