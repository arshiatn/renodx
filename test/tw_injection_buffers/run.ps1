# mods::shader's native D3D11 injection buffers (tests.cpp: PushNativeD3D11Injection on WARP, ReShade's command list
# mocked). No game build needed. Run as x86 (Attila's) and amd64. -Hardware: case 8 alone, in its own process, on the
# hardware adapter (a GPU test: run it in a GPU window, amd64). -OutDir: where the test exe goes (default
# testbin\tw_injection_buffers_<arch>, the maintainer's excluded folder).
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'x86', [switch]$Hardware, [string]$OutDir = '')
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repoRoot 'test\tw_injection_buffers'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
# Test programs only in the maintainer's excluded folder.
$testBuild = if ($OutDir -ne '') { $OutDir } else { Join-Path $env:USERPROFILE ".claude\projects\D--Dev-renodx\tools\tw\testbin\tw_injection_buffers_$Architecture" }
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /bigobj /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $repoRoot /I "$repoRoot\external\reshade" /I "$repoRoot\external\frozen\include" /I "$repoRoot\external\gtl\include" /I "$repoRoot\external\Detours\include" "$testRoot\tests.cpp" /Fe:injection_buffers.exe /link /OPT:REF /OPT:ICF user32.lib advapi32.lib d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib dxguid.lib
if ($LASTEXITCODE -ne 0) { throw 'Injection buffers test compile failed' }
$mode = @(if ($Hardware) { 'hardware' })
& (Join-Path $testBuild 'injection_buffers.exe') @mode
if ($LASTEXITCODE -ne 0) { throw 'Injection buffers test failed' }
