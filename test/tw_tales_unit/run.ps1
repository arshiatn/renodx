# Tales of Arise's own tests (src/games/talesofarise/tests): test/tw_expeditions_unit's runner for this game (build the
# talesofarise target first). -OutDir, -Tests: as there.
param([string]$OutDir = '', [string]$Tests = '')
& (Join-Path $PSScriptRoot '..\tw_expeditions_unit\run.ps1') -OutDir $OutDir -Tests $Tests -Game talesofarise
