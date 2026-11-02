[CmdletBinding(SupportsShouldProcess=$true)]
param([string]$GameDirectory)
. (Join-Path $PSScriptRoot 'Common.ps1')
$root=Split-Path -Parent $PSScriptRoot
$location=Join-Path $root '.local-state\game-directory.txt'
if(-not $GameDirectory -and (Test-Path -LiteralPath $location)){$GameDirectory=[IO.File]::ReadAllText($location).Trim()}
$game=Resolve-AC7Game $GameDirectory
Assert-AC7Closed
$files=[ordered]@{}
foreach($relative in @('AC7DLSS.asi')+@(Get-AC7RuntimeNames | ForEach-Object {'AC7DLSS\'+$_})){
    if(Test-Path -LiteralPath (Join-Path $game $relative)){$files[$relative]=$null}
}
if(-not $files.Count){Write-Output 'AC7 DLSS Enhancer is not installed.';return}
if(-not $PSCmdlet.ShouldProcess($game,'Remove AC7 DLSS Enhancer binaries, preserving settings and captures')){return}
$backup=Join-Path $root ('backups\uninstall-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))
Update-AC7Files $game $files $backup
Write-Output "Uninstalled AC7 DLSS Enhancer. Previous files: $backup"
Write-Output 'Settings, cache, logs, captures, legacy files and other mods were preserved.'
