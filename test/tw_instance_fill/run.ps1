# Parallel Instance Fill (twattila/instance_fill.hpp on the real twattila/worker_pool.hpp, tests.cpp): empire.retail.dll is only read from the
# game folder and mapped twice (relocated, never loaded as a DLL), as test/tw_draw_recording_loop does; the game's own record copies run in the
# mapped copies, the pool's threads are real. x86 only (`-Architecture amd64` only proves the test compiles to its skip). Runs in its own
# process; nothing outside it changes. Test programs only in the maintainer's excluded folder.
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'x86',
      [string]$GameDll = 'C:\Program Files (x86)\Steam\steamapps\common\Total War Attila\empire.retail.dll',
      [string]$BuildFolder = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_instance_fill'
$external = Join-Path $repairRoot 'external'
if (-not (Get-ChildItem -LiteralPath (Join-Path $external 'reshade') -Force -ErrorAction SilentlyContinue)) { $external = 'D:\Dev\renodx\external' }
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
$testBuild = if ($BuildFolder -ne '') { $BuildFolder } else { Join-Path $env:USERPROFILE ".claude\projects\D--Dev-renodx\tools\tw\testbin\tw_instance_fill_$Architecture" }
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$testBuild = (Resolve-Path -LiteralPath $testBuild).Path
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 /DTWATTILA_DRAW_RECORDING /DTWATTILA_INSTANCE_FILL_CONTROLS /DTWATTILA_DRAW_RECORDING_CONTROLS /DTWATTILA_WORKER_POOL_CONTROLS -fms-extensions -Wno-deprecated-declarations -Wno-cast-calling-convention /I $repairRoot /I "$external\reshade" /I "$external\reshade\include" /I "$external\frozen\include" /I "$external\Detours\include" /I "$external\Detours\src" /I "$external\gtl\include" "$testRoot\tests.cpp" /Fe:instance_fill.exe /link /OPT:REF /OPT:ICF user32.lib
if ($LASTEXITCODE -ne 0) { throw 'Parallel Instance Fill test compile failed' }
& (Join-Path $testBuild 'instance_fill.exe') $GameDll
if ($LASTEXITCODE -ne 0) { throw 'Parallel Instance Fill test failed' }
