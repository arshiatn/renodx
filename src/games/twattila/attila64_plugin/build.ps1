# Builds renodx_attila64.dll (the 64-bit Attila loader's plugin for the Attila add-on; see main.cpp) into
# build\attila64_plugin (or -Out). It is not a CMake target (the repository's targets are addons): clang-cl for the
# Linux calling convention and guaranteed tail calls, kernel32 only. The player puts it into the loader's plugins folder
# under exactly this name.
# -Test also builds the tests into -TestOut and runs them with the DLL (elf_sites_test with -Elf <the game's Linux
# executable>, else it reports SKIPPED); the exit code is the number of failures.
param([string]$Out = '', [switch]$Test, [string]$TestOut = '', [string]$Elf = '')
$root = Resolve-Path "$PSScriptRoot\..\..\..\.."
if (-not $Out) { $Out = Join-Path $root "build\attila64_plugin" }
New-Item -ItemType Directory -Force $Out | Out-Null
Set-Location $Out
# Any failed step ends the script with exit 1.
trap { break }
$clang = "C:\Program Files\LLVM\bin\clang-cl.exe"
$flags = @('/nologo', '-std:c++20', '/O2', '/MT', '/EHsc', '/DWIN32', '/D_WINDOWS', '-DNOMINMAX')
# The old DLL goes first: a failed build leaves none and fails the script.
Remove-Item "$Out\renodx_attila64.dll" -ErrorAction SilentlyContinue
if (Test-Path "$Out\renodx_attila64.dll") { throw "$Out\renodx_attila64.dll could not be deleted (still loaded?)" }
& $clang @flags /LD "$PSScriptRoot\main.cpp" "/Fe$Out\renodx_attila64.dll" /link kernel32.lib
if ($LASTEXITCODE -ne 0) { throw "renodx_attila64.dll failed to build (clang-cl exit $LASTEXITCODE)" }
if (-not $Test) { exit 0 }

if (-not $TestOut) { throw "-Test needs -TestOut <folder for the test executables>" }
New-Item -ItemType Directory -Force $TestOut | Out-Null
$failures = 0
foreach ($name in @('patcher_test', 'elf_sites_test')) {
  Remove-Item "$TestOut\$name.exe" -ErrorAction SilentlyContinue
  & $clang @flags "$PSScriptRoot\tests\$name.cpp" "/Fe$TestOut\$name.exe" /link kernel32.lib
  if ($LASTEXITCODE -ne 0) { throw "$name.exe failed to build (clang-cl exit $LASTEXITCODE)" }
}
& "$TestOut\patcher_test.exe" "$Out\renodx_attila64.dll"
$failures += $LASTEXITCODE
if ($Elf) { & "$TestOut\elf_sites_test.exe" "$Out\renodx_attila64.dll" $Elf } else { & "$TestOut\elf_sites_test.exe" }
$failures += $LASTEXITCODE
"tests: $failures failure(s)"
exit $failures
