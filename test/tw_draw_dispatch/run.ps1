# command_action's fast dispatch against RunCallbacks, command by command (tests.cpp): handle reuse, Register after bind,
# the multi-stage fallback, a deferred context, device loss, mixed modules, two modules (partner.cpp, a DLL), and a lying
# gate as the control. No GPU and no game build needed. Run as x86 (Attila, Rome II, Thrones) and amd64. -OutDir: where
# the test exe goes (default build\tw_draw_dispatch_test_<arch>).
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'x86', [string]$OutDir = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_draw_dispatch'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
$testBuild = if ($OutDir -ne '') { $OutDir } else { Join-Path $repairRoot "build\tw_draw_dispatch_test_$Architecture" }
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
# Not "...dispatch...exe": Windows' installer detection asks to elevate an unmanifested x86 exe named "*patch*".
$exe = "fast_draws_$Architecture.exe"
$partner = "fast_draws_partner_$Architecture.dll"
$compile = @("--target=$triple", '/nologo', '/std:c++20', '/EHsc', '/O2', '/MT', '/bigobj', '/DNOMINMAX', '/DWIN32=1', '-Wno-deprecated-declarations',
  '/I', $repairRoot, '/I', "$repairRoot\external\reshade", '/I', "$repairRoot\external\frozen\include", '/I', "$repairRoot\external\Detours\include",
  '/I', "$repairRoot\external\gtl\include")
$libraries = @('user32.lib', 'advapi32.lib', 'd3d11.lib', 'dxgi.lib', 'd3dcompiler.lib', 'dxguid.lib')
# Case 8's second module (partner.cpp), loaded by the exe from its folder.
& 'C:\Program Files\LLVM\bin\clang-cl.exe' @compile /LD "$testRoot\partner.cpp" /Fe:$partner /link /OPT:REF /OPT:ICF @libraries
if ($LASTEXITCODE -ne 0) { throw 'Draw dispatch partner compile failed' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' @compile "$testRoot\tests.cpp" /Fe:$exe /link /OPT:REF /OPT:ICF @libraries
if ($LASTEXITCODE -ne 0) { throw 'Draw dispatch test compile failed' }
& (Join-Path $testBuild $exe)
if ($LASTEXITCODE -ne 0) { throw 'Draw dispatch test failed' }
