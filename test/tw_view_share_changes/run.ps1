# view_share.hpp's Patch against a base commit's (tests.cpp): every pixel shader of the seven view_share games' dumps (only
# read) whose patched bytes differ, on WARP. This tree's view_share.hpp Reflect / Patch (as view_share_patch), -Base's (as
# view_share_base, from git), screen_space_shadows.hpp's Patch / PatchAmbient (as ambient_patch) and water_effects.hpp's
# water patches (as water_patch) are copied into the build folder's ambient_patch.h (the add-on's headers need ReShade's).
# No GPU work. CHANGES_GAME / CHANGES_HASH (tests.cpp) narrow it.
param([string]$Base = 'atn-mods', [string]$Dumps = 'D:\Dev\renodx\bin\cmd_Decompiler-1.3.16')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_view_share_changes'
# Test programs only in the maintainer's excluded folder.
$testBuild = Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\tw\testbin\tw_view_share_changes'
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
# This tree's view_share.hpp and the base's: the same span of each, its WHOLE_TEXTURES (this tree's) after it.
Set-Location -LiteralPath $repairRoot
$baseShare = (& git show "${Base}:src/games/twattila/view_share.hpp") -join "`n"
if ($LASTEXITCODE -ne 0) { throw "git show $Base failed" }
foreach ($pair in @(@('view_share_patch', [IO.File]::ReadAllText((Join-Path $twattila 'view_share.hpp'))), @('view_share_base', $baseShare))) {
  $share = $pair[1]
  $shareStart = $share.IndexOf('constexpr uint32_t SLOT = 12;')
  $shareEnd = $share.IndexOf("// A shader's region read slots", $shareStart)
  if ($shareStart -lt 0 -or $shareEnd -lt 0) { throw "view_share.hpp ($($pair[0])) changed: update run.ps1" }
  $wholeNames = ($share -split "`r?`n" | Where-Object { $_ -match '^constexpr std::array<const char\*, \d+> WHOLE_TEXTURES' }) -join "`n"
  if ($wholeNames -eq '') { throw "view_share.hpp ($($pair[0]), WHOLE_TEXTURES) changed: update run.ps1" }
  $header += "namespace $($pair[0]) {`nusing namespace twattila;`n$($share.Substring($shareStart, $shareEnd - $shareStart))`n$wholeNames`n}  // namespace $($pair[0])`n"
}
[IO.File]::WriteAllText((Join-Path $testBuild 'ambient_patch.h'), $header)
$env:PATH = "C:\Program Files (x86)\Microsoft Visual Studio\Installer;$env:PATH"
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 "/I$testBuild" "$testRoot\tests.cpp" /Fe:view_share_changes.exe /link /OPT:REF /OPT:ICF d3d11.lib dxgi.lib d3dcompiler.lib dxguid.lib
if ($LASTEXITCODE -ne 0) { throw 'view share changes test compile failed' }
& (Join-Path $testBuild 'view_share_changes.exe') $repairRoot $Dumps
if ($LASTEXITCODE -ne 0) { throw 'view share changes test failed' }
