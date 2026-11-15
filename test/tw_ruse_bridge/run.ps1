# R.U.S.E.'s D3D9 bridge on the GPU (tests.cpp: H1 the shared textures, INTZ, the 10-bit round trip, the motion and the
# jitter; H2 the helper's anti-aliasing through Evaluate). Build renodx-ruse.addon64 first (its bridge shaders in
# build\ruse.include) and, for H2, the helper (src\games\twattila\dlaa_helper\build.ps1: its folder's exe and DLLs are
# copied beside the test; without them H2 is skipped). A GPU job: it waits for tools\tw\gpu_job.lock. -Census: the census
# cso folder (the ground VS 0xF01502EA); -Upscaler dlss / fsr / xess for H2; -CompileOnly builds the exe alone (no GPU).
# The game is 64-bit: amd64 only.
param(
  [string]$Census = 'C:\Users\arshi\.claude\projects\D--Dev-renodx\tools\nontw\ruse\census\cso',
  [string]$Helper = '',
  [ValidateSet('dlss', 'fsr', 'xess')][string]$Upscaler = 'dlss',
  [switch]$CompileOnly
)
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repairRoot 'test\tw_ruse_bridge'
$testBuild = Join-Path $repairRoot 'build\tw_ruse_bridge_test'
if ($Helper -eq '') { $Helper = Join-Path $repairRoot 'build\tw_dlaa_helper' }
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $testBuild
& 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 -Wno-deprecated-declarations /I $repairRoot /I "$repairRoot\build\ruse.include" /I "$repairRoot\external\reshade" /I "$repairRoot\external\frozen\include" /I "$repairRoot\external\Detours\include" /I "$repairRoot\external\gtl\include" /I "$repairRoot\external\minhook\include" "$testRoot\tests.cpp" "$repairRoot\external\minhook\src\buffer.c" "$repairRoot\external\minhook\src\hook.c" "$repairRoot\external\minhook\src\trampoline.c" "$repairRoot\external\minhook\src\hde\hde64.c" /Fe:ruse_bridge.exe /link /OPT:REF /OPT:ICF user32.lib gdi32.lib d3d9.lib d3d11.lib dxgi.lib d3dcompiler.lib opengl32.lib "$repairRoot\external\Detours\lib.X64\detours.lib"
if ($LASTEXITCODE -ne 0) { throw 'R.U.S.E. bridge test compile failed' }
if ($CompileOnly) { return }
foreach ($file in Get-ChildItem -LiteralPath $Helper -File -ErrorAction SilentlyContinue | Where-Object { $_.Extension -in '.exe', '.dll' }) {
  Copy-Item -LiteralPath $file.FullName -Destination $testBuild -Force
}
$lock = 'C:\Users\arshi\.claude\projects\D--Dev-renodx\tools\tw\gpu_job.lock'
while ($true) {
  try {
    $file = [System.IO.File]::Open($lock, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write)
    $bytes = [System.Text.Encoding]::UTF8.GetBytes("tw_ruse_bridge $PID $(Get-Date -Format s)")
    $file.Write($bytes, 0, $bytes.Length)
    $file.Close()
    break
  } catch {
    Start-Sleep -Seconds 30
  }
}
try {
  & (Join-Path $testBuild 'ruse_bridge.exe') $Census $Upscaler
  if ($LASTEXITCODE -ne 0) { throw 'R.U.S.E. bridge test failed' }
} finally {
  Remove-Item -LiteralPath $lock -Force -ErrorAction SilentlyContinue
}
