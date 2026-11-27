# Builds renodx_attila64.dll (the 64-bit Attila loader's plugin for the Attila add-on; see main.cpp) into
# build\attila64_plugin (or -Out), with its sha256 beside it (renodx_attila64.dll.sha256, for a64_ab). It is not a CMake
# target (the repository's targets are addons): clang-cl for the Linux calling convention and guaranteed tail calls,
# kernel32 only. The player puts it into the loader's plugins folder under exactly this name.
# -Test also builds the tests into -TestOut and runs them (patcher_test, tweaks_test and elf_sites_test with the DLL,
# battle_core_test on its own; elf_sites_test with -Elf <the game's Linux
# executable>; without it, or with the game's address range taken, that test is SKIPPED and says so in the last line);
# the exit code is the number of failures.
param([string]$Out = '', [switch]$Test, [string]$TestOut = '', [string]$Elf = '')
$root = Resolve-Path "$PSScriptRoot\..\..\..\.."
if (-not $Out) { $Out = Join-Path $root "build\attila64_plugin" }
# Relative folders are taken from where the script was started, before any Set-Location.
$Out = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Out)
if ($TestOut) { $TestOut = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($TestOut) }
New-Item -ItemType Directory -Force $Out | Out-Null
Set-Location $Out
# Any failed step ends the script with exit 1.
trap { break }
$clang = "C:\Program Files\LLVM\bin\clang-cl.exe"
$flags = @('/nologo', '-std:c++20', '/O2', '/MT', '/EHsc', '/DWIN32', '/D_WINDOWS', '-DNOMINMAX')
# The old DLL and its sidecar go first: a failed build leaves neither and fails the script.
Remove-Item "$Out\renodx_attila64.dll", "$Out\renodx_attila64.dll.sha256" -ErrorAction SilentlyContinue
if (Test-Path "$Out\renodx_attila64.dll") { throw "$Out\renodx_attila64.dll could not be deleted (still loaded?)" }
# /Brepro: no link time in the file, so the same sources give the same sha.
& $clang @flags /LD "$PSScriptRoot\main.cpp" "/Fe$Out\renodx_attila64.dll" /link kernel32.lib /Brepro
if ($LASTEXITCODE -ne 0) { throw "renodx_attila64.dll failed to build (clang-cl exit $LASTEXITCODE)" }
# The sidecar a64_ab checks the copied DLL against: "<sha256>  <file name>".
$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath "$Out\renodx_attila64.dll").Hash.ToLower()
[IO.File]::WriteAllText("$Out\renodx_attila64.dll.sha256", "$hash  renodx_attila64.dll`n")
if (-not $Test) { exit 0 }

if (-not $TestOut) { throw "-Test needs -TestOut <folder for the test executables>" }
New-Item -ItemType Directory -Force $TestOut | Out-Null
# The tests' intermediate files stay out of the DLL's folder.
Set-Location $TestOut
foreach ($name in @('patcher_test', 'tweaks_test', 'elf_sites_test', 'battle_core_test')) {
  Remove-Item "$TestOut\$name.exe" -ErrorAction SilentlyContinue
  # tweaks_test takes the C library as a DLL, as the loader exe does: the frame cap goes through that library's getenv.
  $test_flags = if ($name -eq 'tweaks_test') { $flags -replace '^/MT$', '/MD' } else { $flags }
  & $clang @test_flags "$PSScriptRoot\tests\$name.cpp" "/Fe$TestOut\$name.exe" /link kernel32.lib
  if ($LASTEXITCODE -ne 0) { throw "$name.exe failed to build (clang-cl exit $LASTEXITCODE)" }
}
& "$TestOut\patcher_test.exe" "$Out\renodx_attila64.dll"
$failures = $LASTEXITCODE
# The engine values, the graphics route and the loader's frame cap; it removes the ReShade.ini and the note it wrote.
& "$TestOut\tweaks_test.exe" "$Out\renodx_attila64.dll"
$failures += $LASTEXITCODE
# The battle core's pick tables, and its live cases on this process's own CPU sets.
& "$TestOut\battle_core_test.exe"
$failures += $LASTEXITCODE
if ($Elf) { & "$TestOut\elf_sites_test.exe" "$Out\renodx_attila64.dll" $Elf } else { & "$TestOut\elf_sites_test.exe" }
# elf_sites_test exits with 77 when it did not run (tests\elf_sites_test.cpp).
$elf_result = if ($LASTEXITCODE -eq 77) { 'elf_sites_test SKIPPED' } else { 'elf_sites_test ran' }
if ($LASTEXITCODE -ne 77) { $failures += $LASTEXITCODE }
"tests: $failures failure(s), $elf_result"
exit $failures
