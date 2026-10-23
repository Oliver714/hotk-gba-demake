param([string]$Distribution = 'Ubuntu-22.04')
$ErrorActionPreference = 'Stop'
$projectPath = $PSScriptRoot.Replace('\', '/')
if ($projectPath -notmatch '^([A-Za-z]):/(.*)$') { throw 'This script expects a Windows drive path.' }
$linuxPath = '/mnt/' + $Matches[1].ToLowerInvariant() + '/' + $Matches[2]
& wsl.exe -d $Distribution --cd $linuxPath -- bash tools/build.sh
if ($LASTEXITCODE -ne 0) { throw "GBA build failed with exit code $LASTEXITCODE" }
Write-Output "ROM: $PSScriptRoot\dist\sango_gba.gba"
