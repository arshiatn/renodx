# A UE game's own tests (src/games/<Game>/tests, no CMake target; Expeditions: Rome's by default, Tales of Arise's through
# test/tw_tales_unit): each compiled with clang-cl as its header says (the add-on's generated headers from
# build\<Game>.include: configure and build the add-on first) and run with the add-on's source folder (the shader tests
# read it). -OutDir: where the exes are built and run (ATN's antivirus-excluded test folder, tools\tw\testbin); else
# build\. -Tests: a subset (file names without .cpp, A,B).
param([string]$OutDir = '', [string]$Tests = '', [string]$Game = 'expeditionsrome')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$source = Join-Path $repairRoot "src\games\$Game"
$testBuild = Join-Path $(if ($OutDir) { $OutDir } else { Join-Path $repairRoot 'build' }) "tw_${Game}_unit"
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$external = Join-Path $repairRoot 'external'
if (-not (Get-ChildItem -LiteralPath (Join-Path $external 'reshade') -Force -ErrorAction SilentlyContinue)) { $external = 'D:\Dev\renodx\external' }
$generated = Join-Path $repairRoot "build\$Game.include"
if (-not (Test-Path -LiteralPath (Join-Path $generated 'embed\shaders.h'))) { throw "No $generated\embed\shaders.h: build the $Game target first" }
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null

$includes = @("/I$repairRoot", "/I$generated", "/I$external\Detours\include", "/I$external\json\include", "/I$external\frozen\include",
              "/I$external\gtl\include", "/I$external\DLSS\include", "/I$external\reshade", "/I$external\reshade\include")
# Detours: the add-on build's library (CMake's detours_build target writes it).
$detours = Join-Path $external 'Detours\lib.X64\detours.lib'
if (-not (Test-Path -LiteralPath $detours)) { throw "No ${detours}: build the $Game target first" }
$libraries = @('d3d11.lib', 'd3d12.lib', 'dxgi.lib', 'd3dcompiler.lib', 'user32.lib', 'advapi32.lib', 'ole32.lib', 'shell32.lib', $detours)
$names = Get-ChildItem -LiteralPath (Join-Path $source 'tests') -Filter '*.cpp' | ForEach-Object { $_.BaseName }
if ($Tests) { $names = $names | Where-Object { ($Tests -split ',') -contains $_ } }
$failed = @()
foreach ($name in $names) {
  $exe = Join-Path $testBuild "$name.exe"
  & 'C:\Program Files\LLVM\bin\clang-cl.exe' /nologo /std:c++20 /EHsc /MTd /DWIN32=1 /D_WINDOWS /D_DEBUG /D_ITERATOR_DEBUG_LEVEL=0 `
      /DNOMINMAX /w @includes (Join-Path $source "tests\$name.cpp") "/Fo$testBuild\$name.obj" "/Fe$exe" /link @libraries
  if ($LASTEXITCODE -ne 0) { "COMPILE FAIL $name"; $failed += $name; continue }
  Push-Location -LiteralPath $testBuild
  try {
    & $exe $source
    if ($LASTEXITCODE -ne 0) { "FAIL $name (exit $LASTEXITCODE)"; $failed += $name } else { "PASS $name" }
  } finally { Pop-Location }
}
if ($failed) { throw "Failed: $($failed -join ', ')" }
'All passed.'
