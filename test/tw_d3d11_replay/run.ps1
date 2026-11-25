# The D3D11 pass replay (replay.cpp; d3d11_pass_speed.md 3.3) built for x86 or amd64 into the test-exe folder, the Bend
# race-map csos (sss_bend*.cs_5_0.hlsl with BEND_SSS_RACE_MAP, the add-ons' fxc flags) beside it. Needs the add-on's
# embedded shaders (build32\twattila.include for x86, build\twattila.include for amd64: build renodx-twattila first).
#   run.ps1 -Architecture x86 -Warp                 builds, then the self test on WARP
#   run.ps1 -Architecture amd64 -Arguments '--exact','D:\Dev\d11_dumps\...\frame_000123'
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'x86', [switch]$Warp, [string[]]$Arguments,
      [string]$Out = 'C:\Users\arshi\.claude\projects\D--Dev-renodx\tools\tw\testbin\scratch\d11i_\replay')
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$build = Join-Path $Out $Architecture
New-Item -ItemType Directory -Force -Path $build | Out-Null
$include = Join-Path $repoRoot $(if ($Architecture -eq 'x86') { 'build32\twattila.include' } else { 'build\twattila.include' })
if (-not (Test-Path -LiteralPath $include)) { throw "No $include (build renodx-twattila for $Architecture first)" }
$cache = Join-Path $repoRoot $(if ($Architecture -eq 'x86') { 'build32\CMakeCache.txt' } else { 'build\CMakeCache.txt' })
$fxc = (Select-String -LiteralPath $cache -Pattern '^FXC_BIN:FILEPATH=(.*)$').Matches[0].Groups[1].Value

# The race-map build: Bend's 5 wrappers as the add-on compiles them, plus the define.
$csos = Join-Path $build 'race_map'
New-Item -ItemType Directory -Force -Path $csos | Out-Null
$flags = @('/D', '__SHADER_TARGET_MAJOR=5', '/D', '__SHADER_TARGET_MINOR=0', '/DNDEBUG', '/Gec', '/nologo', '/O3', '/Qstrip_reflect', '-E', 'main',
           '-T', 'cs_5_0', '/D', 'BEND_SSS_RACE_MAP')
foreach ($shader in Get-ChildItem -LiteralPath (Join-Path $repoRoot 'src\games\twattila') -Filter 'sss_bend*.cs_5_0.hlsl') {
  $name = $shader.Name -replace '\.cs_5_0\.hlsl$', ''
  # Native tools' warnings on stderr are not errors (their exit codes are checked).
  $ErrorActionPreference = 'Continue'
  & $fxc $shader.FullName @flags -Fo (Join-Path $csos "$name.race_map.cso") 2>&1 | Out-Null
  $ErrorActionPreference = 'Stop'
  if ($LASTEXITCODE -ne 0) { throw "fxc failed: $($shader.Name) with BEND_SSS_RACE_MAP" }
}

$nvapi = Join-Path $repoRoot 'build\downloads\nvapi\extracted\nvapi-main'
if (-not (Test-Path -LiteralPath (Join-Path $nvapi 'nvapi.h'))) { $nvapi = 'D:\Dev\renodx\build\downloads\nvapi\extracted\nvapi-main' }
$nvapiLib = Join-Path $nvapi $(if ($Architecture -eq 'x86') { 'x86\nvapi.lib' } else { 'amd64\nvapi64.lib' })
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation | Out-Null
Push-Location -LiteralPath $build
try {
  $ErrorActionPreference = 'Continue'
  $triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
  & 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations `
    /I $repoRoot /I $include /I "$repoRoot\external\reshade" /I "$repoRoot\external\frozen\include" /I "$repoRoot\external\Detours\include" `
    /I "$repoRoot\external\gtl\include" /I "$repoRoot\external\json\include" /I $nvapi (Join-Path $PSScriptRoot 'replay.cpp') /Fe:replay.exe `
    /link /OPT:REF /OPT:ICF user32.lib d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib $nvapiLib
  if ($LASTEXITCODE -ne 0) { throw 'replay compile failed' }
  $ErrorActionPreference = 'Stop'
} finally {
  Pop-Location
}
$exe = Join-Path $build 'replay.exe'
if (-not $Arguments) { $Arguments = @('--self-test', (Join-Path $build 'self_test')) }
if ($Warp) { $Arguments += '--warp' }
if ($Arguments[0] -ne '--profile') { $Arguments += @('--race-map', $csos) }
& $exe @Arguments
exit $LASTEXITCODE
