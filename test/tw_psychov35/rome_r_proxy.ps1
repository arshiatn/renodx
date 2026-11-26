# Rome Remastered's swap chain proxy on WARP (rome_r_proxy.cpp): compiled with the build's flags from this tree and from
# $Before (the commit before its clamps came back, O-CO16). -OutDir: where the test exe is built and run (ATN's
# antivirus-excluded test folder, tools\tw\testbin); else build\. -CMakeCache: another tree's, for a worktree without a
# build. Run as x86 and amd64.
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'amd64', [string]$Before = '4f90d9473', [string]$OutDir = '',
      [string]$CMakeCache = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_psychov35'
$testBuild = if ($OutDir) { Join-Path $OutDir "rome_r_proxy_$Architecture" } else { Join-Path $repairRoot "build\rome_r_proxy_$Architecture" }
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null

$cache = @('build\CMakeCache.txt', 'build32\CMakeCache.txt') | ForEach-Object { Join-Path $repairRoot $_ } |
         Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if ($CMakeCache) { $cache = $CMakeCache }
$fxc = (Select-String -LiteralPath $cache -Pattern '^FXC_BIN:FILEPATH=(.*)$').Matches[0].Groups[1].Value
$beforeTree = Join-Path $testBuild 'before'
if (Test-Path -LiteralPath $beforeTree) { Remove-Item -LiteralPath $beforeTree -Recurse -Force -Confirm:$false }
New-Item -ItemType Directory -Force -Path $beforeTree | Out-Null
$archive = Join-Path $testBuild 'before.tar'
& git -C $repairRoot archive --output=$archive $Before src/games/twromeremastered src/shaders
if ($LASTEXITCODE -ne 0) { throw 'git archive failed' }
& (Join-Path $env:SystemRoot 'System32\tar.exe') -xf $archive -C $beforeTree
if ($LASTEXITCODE -ne 0) { throw 'tar failed' }

# The build's flags (CMakeLists.txt): ps_5_x builds as ps_5_0, NDEBUG, /Gec, /O3.
$ErrorActionPreference = 'Continue'
foreach ($side in @(@{ Root = $beforeTree; Out = 'before.cso' }, @{ Root = $repairRoot; Out = 'after.cso' })) {
  & $fxc (Join-Path $side.Root 'src\games\twromeremastered\swap_chain_proxy_pixel_shader.ps_5_x.hlsl') /T ps_5_0 `
      /D __SHADER_TARGET_MAJOR=5 /D __SHADER_TARGET_MINOR=0 /DNDEBUG /Gec /nologo /O3 /E main `
      /Fo (Join-Path $testBuild $side.Out) 2>$null | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "fxc failed: $($side.Root)" }
}

$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 -Wno-macro-redefined `
    /I $repairRoot "$testRoot\rome_r_proxy.cpp" /Fe:rome_r_proxy.exe /link /OPT:REF /OPT:ICF d3d11.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) { throw 'Rome R proxy test compile failed' }
& (Join-Path $testBuild 'rome_r_proxy.exe') (Join-Path $testBuild 'before.cso') (Join-Path $testBuild 'after.cso')
if ($LASTEXITCODE -ne 0) { throw 'Rome R proxy test failed' }
