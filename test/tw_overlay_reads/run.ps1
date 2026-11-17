# The overlays' readers below 100% (tests.cpp): every overlay RENDER_SCALE_READS row of Attila, Thrones of Britannia, Rome II,
# Warhammer, Warhammer II, Troy and Pharaoh, the game's bytes from their dumps (only read), on WARP. view_share.hpp's Reflect and
# Patch (as view_share_patch), screen_space_shadows.hpp's Patch / PatchAmbient (as ambient_patch) and water_effects.hpp's water
# patches (as water_patch) are copied out of this tree into the build folder's ambient_patch.h (the add-on's headers need
# ReShade's). Holds tools/tw's GPU job lock while it runs when -Lock names it.
param([string]$Lock = '', [string]$Dumps = 'D:\Dev\renodx\bin\cmd_Decompiler-1.3.16')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_overlay_reads'
# Test programs only in the maintainer's excluded folder.
$testBuild = Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\tw\testbin\tw_overlay_reads'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$twattila = Join-Path $repairRoot 'src\games\twattila'
$shadows = [IO.File]::ReadAllText((Join-Path $twattila 'screen_space_shadows.hpp'))
$start = $shadows.IndexOf('inline std::vector<uint8_t> Patch(std::span<const uint8_t> code')
$end = $shadows.IndexOf('struct LightObjects', $start)
if ($start -lt 0 -or $end -lt 0) { throw 'screen_space_shadows.hpp changed: update run.ps1' }
$slots = ($shadows -split "`r?`n" | Where-Object { $_ -match '^constexpr UINT \w+_SLOT' }) -join "`n"
$harmonics = ([IO.File]::ReadAllLines((Join-Path $twattila 'light_bounce.hpp')) | Where-Object { $_ -match '^constexpr uint32_t AMBIENT_AFTER_HARMONICS' }) -join "`n"
$dxbc = (Join-Path $twattila 'dxbc.hpp') -replace '\\', '/'
$header = "#pragma once`n#include <format>`n#include `"$dxbc`"`nnamespace twattila::light_bounce {`n$harmonics`n}`n" +
          "namespace ambient_patch {`nusing namespace twattila;`n$slots`n$($shadows.Substring($start, $end - $start))`n}  // namespace ambient_patch`n"
$water = [IO.File]::ReadAllText((Join-Path $twattila 'water_effects.hpp'))
$typesStart = $water.IndexOf('struct WaterOpacity {')
$typesEnd = $water.IndexOf("// The game's water shaders, read per game", $typesStart)
$patchStart = $water.IndexOf('inline std::vector<uint8_t> PatchWaterCaustics(')
$lineStart = $water.IndexOf('inline std::vector<uint8_t> PatchWaterScaleLine(', $patchStart)
$patchEnd = $water.IndexOf("`n}", $lineStart)
if ($typesStart -lt 0 -or $typesEnd -lt 0 -or $patchStart -lt 0 -or $lineStart -lt 0 -or $patchEnd -lt 0) { throw 'water_effects.hpp changed: update run.ps1' }
$waterConstants = ($water -split "`r?`n" | Where-Object { $_ -match '^constexpr (UINT|float) WATER_' }) -join "`n"
$header += "namespace water_patch {`nusing namespace twattila;`n$waterConstants`n$($water.Substring($typesStart, $typesEnd - $typesStart))`n" +
           "$($water.Substring($patchStart, $patchEnd + 2 - $patchStart))`n}  // namespace water_patch`n"
$share = [IO.File]::ReadAllText((Join-Path $twattila 'view_share.hpp'))
$shareStart = $share.IndexOf('constexpr uint32_t SLOT = 12;')
$shareEnd = $share.IndexOf("// A shader's region read slots", $shareStart)
if ($shareStart -lt 0 -or $shareEnd -lt 0) { throw 'view_share.hpp changed: update run.ps1' }
$wholeNames = ($share -split "`r?`n" | Where-Object { $_ -match '^constexpr std::array<const char\*, \d+> WHOLE_TEXTURES' }) -join "`n"
if ($wholeNames -eq '') { throw 'view_share.hpp changed (WHOLE_TEXTURES): update run.ps1' }
$header += "namespace view_share_patch {`nusing namespace twattila;`n$($share.Substring($shareStart, $shareEnd - $shareStart))`n$wholeNames`n}  // namespace view_share_patch`n"
[IO.File]::WriteAllText((Join-Path $testBuild 'ambient_patch.h'), $header)
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 "/I$testBuild" "$testRoot\tests.cpp" /Fe:overlay_reads.exe /link /OPT:REF /OPT:ICF d3d11.lib dxgi.lib d3dcompiler.lib dxguid.lib
if ($LASTEXITCODE -ne 0) { throw 'overlay reads test compile failed' }
# The lock: made only where none exists (CreateNew), kept only if it still holds this run's line 3 s later, removed only
# while it is this run's.
$mine = "tw_overlay_reads (WARP) $PID $(Get-Date -Format s)"
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
  & (Join-Path $testBuild 'overlay_reads.exe') $repairRoot $Dumps
  if ($LASTEXITCODE -ne 0) { throw 'overlay reads test failed' }
} finally {
  if ($Lock -ne '' -and (& $held)) { Remove-Item -LiteralPath $Lock -Force -Confirm:$false }
}
