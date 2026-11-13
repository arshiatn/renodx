# Builds scale_census_replay.exe into -Out (a scratch folder, not the repo) from the repo's current twattila/view_share.hpp
# (scale_census_hook.py writes the hooked copy there first).
# usage: powershell -NoProfile -ExecutionPolicy Bypass -File scale_census_build.ps1 -Out <scratch folder> [-Repo D:\Dev\renodx]
param([Parameter(Mandatory = $true)][string]$Out, [string]$Repo = 'D:\Dev\renodx')
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
New-Item -ItemType Directory -Force $Out | Out-Null
& python -I (Join-Path $here 'scale_census_hook.py') $Repo (Join-Path $Out 'view_share_hooked.hpp')
if ($LASTEXITCODE -ne 0) { throw 'scale_census_hook failed' }
$vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
Push-Location $Out
try {
  & 'C:\Program Files\LLVM\bin\clang-cl.exe' --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32=1 -Wno-everything `
      /I $Out /I $Repo /I "$Repo\external\reshade" /I "$Repo\external\reshade\include" /I "$Repo\external\frozen\include" `
      /I "$Repo\external\Detours\include" /I "$Repo\external\gtl\include" (Join-Path $here 'scale_census_replay.cpp') `
      /Fe:scale_census_replay.exe /link /OPT:REF user32.lib d3d11.lib dxguid.lib
  if ($LASTEXITCODE -ne 0) { throw 'scale_census_replay compile failed' }
} finally {
  Pop-Location
}
