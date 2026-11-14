# Total War: WARHAMMER's sun light shaders (the game's bytes from a shader dump: its cso folder, only read), WH1's rt_sun,
# the ambient patch and the SSR resolve replacement on WARP (tests.cpp). screen_space_shadows.hpp's Patch and PatchAmbient
# are copied out of this tree into the build folder's ambient_patch.h (the add-on's header needs ReShade's), and out of
# -Before (the commit before Warhammer's AMBIENT_AFTER_HARMONICS_Z) for the other games' bytes. Holds tools/tw's GPU job
# lock while it runs when -Lock names it: created only when it doesn't exist (CreateNew), kept only if it still holds this
# run's line 3 s later (else waits again, the file left alone), deleted only while it is this run's.
param([Parameter(Mandatory = $true)][string]$Dump, [ValidateSet('x86', 'amd64')][string]$Architecture = 'amd64', [string]$Lock = '',
      [string]$Before = 'df1aff82')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_warhammer_shaders'
$testBuild = Join-Path $repairRoot "build\tw_warhammer_shaders_test_$Architecture"
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$shadows = [IO.File]::ReadAllText((Join-Path $repairRoot 'src\games\twattila\screen_space_shadows.hpp'))
$start = $shadows.IndexOf('inline std::vector<uint8_t> Patch(std::span<const uint8_t> code')
$end = $shadows.IndexOf('struct LightObjects', $start)
if ($start -lt 0 -or $end -lt 0) { throw 'screen_space_shadows.hpp changed: update run.ps1' }
$slots = ($shadows -split "`r?`n" | Where-Object { $_ -match '^constexpr UINT \w+_SLOT' }) -join "`n"
$harmonics = ([IO.File]::ReadAllLines((Join-Path $repairRoot 'src\games\twattila\light_bounce.hpp')) | Where-Object { $_ -match '^constexpr uint32_t AMBIENT_AFTER_HARMONICS' }) -join "`n"
$dxbc = (Join-Path $repairRoot 'src\games\twattila\dxbc.hpp') -replace '\\', '/'
$header = "#pragma once`n#include `"$dxbc`"`nnamespace twattila::light_bounce {`n$harmonics`n}`n" +
          "namespace ambient_patch {`nusing namespace twattila;`n$slots`n$($shadows.Substring($start, $end - $start))`n}  // namespace ambient_patch`n"
# The same functions at $Before (the other games' bytes must stay the same).
$old = (& git -C $repairRoot show "${Before}:src/games/twattila/screen_space_shadows.hpp") -join "`n"
if ($LASTEXITCODE -ne 0) { throw "git show $Before failed" }
$oldStart = $old.IndexOf('inline std::vector<uint8_t> Patch(std::span<const uint8_t> code')
$oldEnd = $old.IndexOf('struct LightObjects', $oldStart)
$header += "namespace ambient_patch_before {`nusing namespace twattila;`n$slots`n$($old.Substring($oldStart, $oldEnd - $oldStart))`n}  // namespace ambient_patch_before`n"
[IO.File]::WriteAllText((Join-Path $testBuild 'ambient_patch.h'), $header)
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
$triple = if ($Architecture -eq 'x86') { 'i686-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
& 'C:\Program Files\LLVM\bin\clang-cl.exe' "--target=$triple" /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 "/I$testBuild" "$testRoot\tests.cpp" /Fe:warhammer_shaders.exe /link /OPT:REF /OPT:ICF d3d11.lib dxgi.lib d3dcompiler.lib dxguid.lib
if ($LASTEXITCODE -ne 0) { throw 'WH1 shader test compile failed' }
$mine = "tw_warhammer_shaders (WARP) $PID $(Get-Date -Format s)"
$held = { try { [IO.File]::ReadAllText($Lock) -eq $mine } catch { $false } }
while ($Lock -ne '') {
  try {
    $stream = [IO.File]::Open($Lock, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    $bytes = [Text.Encoding]::ASCII.GetBytes($mine)
    $stream.Write($bytes, 0, $bytes.Length)
    $stream.Close()
    Start-Sleep -Seconds 3
    if (& $held) { break }
  } catch {
  }
  Start-Sleep -Seconds 30
}
try {
  & (Join-Path $testBuild 'warhammer_shaders.exe') $repairRoot $Dump
  if ($LASTEXITCODE -ne 0) { throw 'WH1 shader test failed' }
} finally {
  if ($Lock -ne '' -and (& $held)) { Remove-Item -LiteralPath $Lock -Force -Confirm:$false }
}
