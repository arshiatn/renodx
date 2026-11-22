# twattila/ao_skip.hpp (tests.cpp): SkipGameSsao's rule over rt_scene's runtime, the frame-0 control. No GPU work, amd64.
# Needs a renodx-twattila.addon32 build's include folder (rt_scene.hpp's embedded shaders): build32\twattila.include, or -Include;
# -External: another checkout's external folder (a worktree's submodules can be empty).
param([string]$Include = '', [string]$External = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
if ($Include -eq '') { $Include = Join-Path $repairRoot 'build32\twattila.include' }
$testRoot = Join-Path $repairRoot 'test\tw_ao_skip'
$external = if ($External -ne '') { $External } else { Join-Path $repairRoot 'external' }
$env:PATH = "C:\Program Files (x86)\Microsoft Visual Studio\Installer;$env:PATH"
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
# Test programs only in the maintainer's excluded folder.
$testBuild = Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\tw\testbin\tw_ao_skip'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /bigobj /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $repairRoot /I $Include /I "$external\reshade" /I "$external\frozen\include" /I "$external\Detours\include" /I "$external\gtl\include" "$testRoot\tests.cpp" /Fe:ao_skip.exe /link /OPT:REF /OPT:ICF "$external\Detours\lib.X64\detours.lib" user32.lib advapi32.lib d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib dxguid.lib
if ($LASTEXITCODE -ne 0) { throw 'AO skip test compile failed' }
& (Join-Path $testBuild 'ao_skip.exe')
if ($LASTEXITCODE -ne 0) { throw 'AO skip test failed' }
