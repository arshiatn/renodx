param([ValidateSet('x86', 'amd64')][string]$Architecture = 'x86')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_native_hook_lifetime'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
$testBuild = Join-Path $repairRoot "build\astra_native_hooks_$Architecture"
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
$detoursArch = if ($Architecture -eq 'x86') { 'X86' } else { 'X64' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /LD /DNOMINMAX /DWIN32=1 /I $repairRoot /I "$repairRoot\external\reshade" /I "$repairRoot\external\frozen\include" /I "$repairRoot\external\Detours\include" /I "$repairRoot\external\gtl\include" "$testRoot\module.cpp" /Fe:native_hooks_test.dll /link /OPT:REF /OPT:ICF "$repairRoot\external\Detours\lib.$detoursArch\detours.lib" user32.lib
if ($LASTEXITCODE -ne 0) { throw 'Native hook test DLL compile failed' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX "$testRoot\host.cpp" /Fe:native_hooks_host.exe
if ($LASTEXITCODE -ne 0) { throw 'Native hook test host compile failed' }
foreach ($stage in 0..3) {
  & (Join-Path $testBuild 'native_hooks_host.exe') $stage
  if ($LASTEXITCODE -ne 0) { throw "Native hook regression failed at stage $stage" }
}
