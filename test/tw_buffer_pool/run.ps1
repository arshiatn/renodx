# twattila/buffer_pool.hpp, Fewer Driver Waits (the constant buffer ring), on WARP (tests.cpp: the 35 bodies, the rules
# it relies on, pixels / compute output / copies on vs off with its controls, Get / Get1 / Map parity, references, the
# full ring and its wrap, another thread's call, the unwind's edges, the device's end, Uninstall on and off the render
# thread). No game build needed. Run as x86 (Attila) and amd64.
# -OutDir: where the test exe goes (default testbin\tw_buffer_pool_<arch>, the maintainer's excluded folder).
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'x86', [string]$OutDir = '')
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repoRoot 'test\tw_buffer_pool'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
$testBuild = if ($OutDir -ne '') { $OutDir } else { Join-Path $env:USERPROFILE ".claude\projects\D--Dev-renodx\tools\tw\testbin\tw_buffer_pool_$Architecture" }
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
$detoursArch = if ($Architecture -eq 'x86') { 'X86' } else { 'X64' }
$exe = "buffer_pool_$Architecture.exe"
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /bigobj /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $repoRoot /I "$repoRoot\external\reshade" /I "$repoRoot\external\frozen\include" /I "$repoRoot\external\gtl\include" /I "$repoRoot\external\Detours\include" "$testRoot\tests.cpp" /Fe:$exe /link /OPT:REF /OPT:ICF "$repoRoot\external\Detours\lib.$detoursArch\detours.lib" user32.lib advapi32.lib d3d11.lib dxgi.lib dxguid.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) { throw 'Buffer pool test compile failed' }
& (Join-Path $testBuild $exe)
if ($LASTEXITCODE -ne 0) { throw 'Buffer pool test failed' }
