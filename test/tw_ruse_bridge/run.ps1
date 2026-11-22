# R.U.S.E.'s D3D9 bridge on the GPU (tests.cpp: H1 the shared textures, INTZ, the 10-bit round trip, the motion and the
# jitter; H2 the helper's anti-aliasing through Evaluate) and its Scene Replay (replay.cpp, -Replay: H3 on WARP, then the
# GPU, against the game's vs_3_0 in tools\nontw\ruse\scripts\ru_replay_check.py's SM3 interpreter). Build
# renodx-ruse.addon64 first (its shaders in build\ruse.include) and, for H2, the helper (src\games\twattila\dlaa_helper\
# build.ps1, or -Helper: its folder's exe and DLLs are copied beside the test, NVIDIA's from -Ngx where it has none;
# without the helper H2 is skipped). Each GPU job waits for tools\tw\gpu_job.lock and lets it go after. -Census: the
# census cso folder (the ground VS 0xF01502EA); -Upscaler dlss / fsr / xess for H2; -CompileOnly builds the exes alone.
# -V35: the V35 fit (v35_fit.cpp) on WARP alone. The game is 64-bit: amd64 only.
param(
  [string]$Census = 'C:\Users\arshi\.claude\projects\D--Dev-renodx\tools\nontw\ruse\census\cso',
  [string]$Helper = '',
  [string]$Ngx = 'C:\Users\arshi\Documents\Nvidia(myself)\streamline-sdk-v2.14.16\bin\x64',
  [ValidateSet('dlss', 'fsr', 'xess')][string]$Upscaler = 'dlss',
  [switch]$Replay,
  [switch]$CompileOnly,
  # The V35 fit (v35_fit.cpp) alone, on WARP: no GPU job, no add-on build needed (the post compiled here).
  [switch]$V35,
  # Test exes go to the folder Bitdefender leaves alone (tools\tw\testbin).
  [string]$Build = 'C:\Users\arshi\.claude\projects\D--Dev-renodx\tools\tw\testbin\tw_ruse_bridge'
)
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_ruse_bridge'
$testBuild = $Build
$scripts = 'C:\Users\arshi\.claude\projects\D--Dev-renodx\tools\nontw\ruse\scripts'
if ($Helper -eq '') { $Helper = Join-Path $repairRoot 'build\tw_dlaa_helper' }
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
Set-Location -LiteralPath $testBuild
if ($V35) {
  # The final post as ps_5_0 with the build's flags (/Gec takes its D3D9 sampler2D / COLOR syntax), as test/tw_psychov35
  # (its warnings on stderr are no failure: the exit code is).
  $ErrorActionPreference = 'Continue'
  & 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe' (Join-Path $repairRoot 'src\games\ruse\shaders\postprocess00_0x232EB5DF.ps_3_0.hlsl') `
      /T ps_5_0 /D __SHADER_TARGET_MAJOR=5 /D __SHADER_TARGET_MINOR=0 /DNDEBUG /Gec /nologo /O3 /E main /Fo (Join-Path $testBuild 'ruse_post.cso') 2>$null | Out-Null
  if ($LASTEXITCODE -ne 0) { throw 'fxc failed: R.U.S.E. post' }
  $ErrorActionPreference = 'Stop'
  & 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 -Wno-macro-redefined `
      /I $repairRoot (Join-Path $testRoot 'v35_fit.cpp') /Fe:ruse_v35_fit.exe /link /OPT:REF /OPT:ICF d3d11.lib d3dcompiler.lib
  if ($LASTEXITCODE -ne 0) { throw 'R.U.S.E. v35_fit.cpp compile failed' }
  & (Join-Path $testBuild 'ruse_v35_fit.exe') $testBuild
  if ($LASTEXITCODE -ne 0) { throw 'R.U.S.E. V35 fit failed' }
  return
}
foreach ($test in @(@('tests.cpp', 'ruse_bridge.exe'), @('replay.cpp', 'ruse_replay.exe'))) {
  & 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations -Wno-unused-command-line-argument /I $repairRoot /I "$repairRoot\build\ruse.include" /I "$repairRoot\external\reshade" /I "$repairRoot\external\frozen\include" /I "$repairRoot\external\Detours\include" /I "$repairRoot\external\gtl\include" /I "$repairRoot\external\minhook\include" (Join-Path $testRoot $test[0]) "$repairRoot\external\minhook\src\buffer.c" "$repairRoot\external\minhook\src\hook.c" "$repairRoot\external\minhook\src\trampoline.c" "$repairRoot\external\minhook\src\hde\hde64.c" "/Fe:$($test[1])" /link /OPT:REF /OPT:ICF user32.lib gdi32.lib d3d9.lib d3d11.lib dxgi.lib d3dcompiler.lib opengl32.lib "$repairRoot\external\Detours\lib.X64\detours.lib"
  if ($LASTEXITCODE -ne 0) { throw "R.U.S.E. $($test[0]) compile failed" }
}
if ($CompileOnly) { return }

# One GPU job under the lock (polled every 2 s: other agents take it between their jobs too).
function Invoke-GpuJob([scriptblock]$Job) {
  $lock = 'C:\Users\arshi\.claude\projects\D--Dev-renodx\tools\tw\gpu_job.lock'
  while ($true) {
    try {
      $file = [System.IO.File]::Open($lock, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write)
      $bytes = [System.Text.Encoding]::UTF8.GetBytes("tw_ruse_bridge $PID $(Get-Date -Format s)")
      $file.Write($bytes, 0, $bytes.Length)
      $file.Close()
      break
    } catch {
      Start-Sleep -Seconds 2
    }
  }
  try {
    & $Job
  } finally {
    Remove-Item -LiteralPath $lock -Force -ErrorAction SilentlyContinue
  }
}

if ($Replay) {
  $cases = Join-Path $testBuild 'replay_cases.bin'
  $described = Join-Path $testBuild 'replay_cases.json'
  & py -I (Join-Path $scripts 'ru_replay_check.py') prepare $cases $described
  if ($LASTEXITCODE -ne 0) { throw 'H3 cases not made' }
  foreach ($driver in 'warp', 'gpu') {
    $positions = Join-Path $testBuild "replay_$driver.bin"
    $run = { & (Join-Path $testBuild 'ruse_replay.exe') $cases $positions $driver }
    if ($driver -eq 'gpu') { Invoke-GpuJob $run } else { & $run }
    if ($LASTEXITCODE -ne 0) { throw "H3 replay on $driver failed" }
    & py -I (Join-Path $scripts 'ru_replay_check.py') check $described $positions
    if ($LASTEXITCODE -ne 0) { throw "H3 on $driver failed" }
  }
  return
}
foreach ($file in Get-ChildItem -LiteralPath $Helper -File -ErrorAction SilentlyContinue | Where-Object { $_.Extension -in '.exe', '.dll' }) {
  Copy-Item -LiteralPath $file.FullName -Destination $testBuild -Force
}
if (Test-Path -LiteralPath (Join-Path $testBuild 'renodx-tw-dlaa64.exe')) {
  foreach ($name in 'nvngx_dlss.dll', 'nvngx_dlssd.dll', 'nvngx_dlssg.dll', 'sl.interposer.dll', 'sl.common.dll', 'sl.dlss_g.dll', 'sl.reflex.dll', 'sl.pcl.dll') {
    if (-not (Test-Path -LiteralPath (Join-Path $Helper $name))) { Copy-Item -LiteralPath (Join-Path $Ngx $name) -Destination $testBuild -Force }
  }
}
Invoke-GpuJob {
  & (Join-Path $testBuild 'ruse_bridge.exe') $Census $Upscaler
  if ($LASTEXITCODE -ne 0) { throw 'R.U.S.E. bridge test failed' }
}
