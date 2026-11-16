# Bend's screen-space shadows and GT-VBGI on the Unreal games (tests.cpp) on WARP, once with Expeditions: Rome's embedded
# shaders (build\expeditionsrome.include) and once with Tales of Arise's (build\talesofarise.include, with Bandai's
# dumped contact shadows: bin\cmd_Decompiler-1.3.16\dump_shaders_tales_of_arise\0x95802ADA.ps_5_0.cso). Build both
# addons (amd64) first. The test programs go to -Out (the agents' excluded test folder).
param([string]$Out = 'C:\Users\arshi\.claude\projects\D--Dev-renodx\tools\tw\testbin\tw_ue_sss_gi')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_ue_sss_gi'
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $Out
$failed = $false
foreach ($game in @(@{ Name = 'expeditionsrome'; Define = '/DUE_GAME_ROME'; Arguments = @() },
                    @{ Name = 'talesofarise'; Define = '/DUE_GAME_TALES'; Arguments = @((Join-Path $repairRoot 'bin\cmd_Decompiler-1.3.16\dump_shaders_tales_of_arise\0x95802ADA.ps_5_0.cso')) })) {
  $exe = "ue_sss_gi_$($game.Name).exe"
  & 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 $game.Define `
    -Wno-deprecated-declarations /I $repairRoot /I "$repairRoot\build\$($game.Name).include" /I "$repairRoot\external\reshade" `
    /I "$repairRoot\external\frozen\include" /I "$repairRoot\external\Detours\include" /I "$repairRoot\external\gtl\include" `
    "$testRoot\tests.cpp" /Fe:$exe /link /OPT:REF /OPT:ICF user32.lib d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib
  if ($LASTEXITCODE -ne 0) { throw "compile failed: $($game.Name)" }
  Write-Host "== $($game.Name)"
  & (Join-Path $Out $exe) @($game.Arguments)
  if ($LASTEXITCODE -ne 0) { $failed = $true }
}
if ($failed) { throw 'UE screen-space shadows / GT-VBGI tests failed' }
