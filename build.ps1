param([string]$Distribution = 'Ubuntu-22.04', [ValidateSet('zh', 'en')][string]$Language = 'zh')
$ErrorActionPreference = 'Stop'
$projectPath = $PSScriptRoot.Replace('\', '/')
if ($projectPath -notmatch '^([A-Za-z]):/(.*)$') { throw 'This script expects a Windows drive path.' }
$linuxPath = '/mnt/' + $Matches[1].ToLowerInvariant() + '/' + $Matches[2]
& wsl.exe -d $Distribution --cd $linuxPath -- env "GAME_LANGUAGE=$Language" bash tools/build.sh
if ($LASTEXITCODE -ne 0) { throw "GBA build failed with exit code $LASTEXITCODE" }
$romName = if ($Language -eq 'en') { 'sango_gba_en.gba' } else { 'sango_gba.gba' }
Write-Output "ROM: $PSScriptRoot\dist\$romName"
