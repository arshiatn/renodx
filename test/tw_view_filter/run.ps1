# The view filter (resource.hpp ViewFilterData, resource_upgrade.hpp GetResourceViewClone) against the locked lookup it
# skips (tests.cpp: its parts, each in a child process). partner.cpp: a second module built with the filter; old.cpp: an
# add-on built before it. Release (NDEBUG: the filter answers; debug builds always take the lookup). No GPU, no game
# build. Run as x86 (Attila, Rome II, Thrones) and amd64. -OutDir: where the exe and DLLs go.
param([ValidateSet('x86', 'amd64')][string]$Architecture = 'x86', [string]$OutDir = '')
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repoRoot 'test\tw_view_filter'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
$testBuild = if ($OutDir -ne '') { $OutDir } else { Join-Path $repoRoot "build\tw_view_filter_test_$Architecture" }
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
$compile = @("--target=$triple", '/nologo', '/std:c++20', '/EHsc', '/O2', '/MT', '/bigobj', '/DNDEBUG', '/DNOMINMAX', '/DWIN32=1',
  '-Wno-deprecated-declarations', '/I', $repoRoot, '/I', "$repoRoot\external\reshade", '/I', "$repoRoot\external\frozen\include",
  '/I', "$repoRoot\external\Detours\include", '/I', "$repoRoot\external\gtl\include")
$libraries = @('user32.lib', 'advapi32.lib', 'd3d11.lib', 'd3d12.lib', 'dxgi.lib')
foreach ($module in @('partner', 'old')) {
  & 'C:\Program Files\LLVM\bin\clang-cl.exe' @compile /LD "$testRoot\$module.cpp" "/Fe:view_filter_${module}_$Architecture.dll" /link /OPT:REF /OPT:ICF @libraries
  if ($LASTEXITCODE -ne 0) { throw "View filter $module compile failed" }
}
& 'C:\Program Files\LLVM\bin\clang-cl.exe' @compile "$testRoot\tests.cpp" "/Fe:view_filter_$Architecture.exe" /link /OPT:REF /OPT:ICF @libraries
if ($LASTEXITCODE -ne 0) { throw 'View filter test compile failed' }
& (Join-Path $testBuild "view_filter_$Architecture.exe")
if ($LASTEXITCODE -ne 0) { throw 'View filter test failed' }
