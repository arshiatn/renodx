param([ValidateSet('x86', 'amd64')][string]$Architecture = 'amd64')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_device_loss'
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
$testBuild = Join-Path $repairRoot "build\tw_device_loss_test_$Architecture"
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 /I $repairRoot /I $testRoot /I "$repairRoot\external\reshade" /I "$repairRoot\external\frozen\include" /I "$repairRoot\external\Detours\include" /I "$repairRoot\external\gtl\include" "$testRoot\tests.cpp" /Fe:device_loss.exe /link /OPT:REF /OPT:ICF user32.lib d3d11.lib d3d12.lib dxgi.lib
if ($LASTEXITCODE -ne 0) { throw 'Device loss test compile failed' }
& (Join-Path $testBuild "device_loss.exe")
if ($LASTEXITCODE -ne 0) { throw "Device loss test failed" }

# The stall dump the test left, opened by a debugger (when one is installed): the faulting thread's and the waiting
# thread's stacks unwind to names. Export names only: no symbol server is asked.
$debugger = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
if (Test-Path -LiteralPath $debugger) {
  $env:_NT_SYMBOL_PATH = $testBuild
  $stacks = (& $debugger -z (Join-Path $testBuild 'renodx-stall-first.dmp') -c '~*kn 12; q' 2>&1 | Out-String)
  if ($Architecture -eq 'amd64' -and $stacks -notmatch 'KiUserExceptionDispatcher') { throw 'Stall dump: the faulting thread does not unwind to the exception dispatcher' }
  if ($stacks -notmatch 'SleepEx|NtDelayExecution|ZwDelayExecution') { throw 'Stall dump: no thread unwinds to its sleep' }
  'Stall dump: opened by cdb, stacks unwind.'
} else {
  'Stall dump: cdb not installed, not opened by a debugger.'
}
