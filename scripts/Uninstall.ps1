[CmdletBinding(SupportsShouldProcess=$true)]
param([string]$GameDirectory)
trap { [Console]::Error.WriteLine('Uninstall failed: ' + $_.Exception.Message); exit 1 }
. (Join-Path $PSScriptRoot 'Common.ps1')
$game=Resolve-AC7Game $GameDirectory
Assert-AC7Closed
$files=[ordered]@{}
foreach($relative in @('AC7DLSS.asi','AC7DLSS.ini')+@(Get-AC7RuntimeNames | ForEach-Object {'AC7DLSS\'+$_})){
    if(Test-Path -LiteralPath (Join-Path $game $relative)){$files[$relative]=$null}
}
if(-not $files.Count){Write-Output 'AC7 DLSS Enhancer is not installed.';return}
if(-not $PSCmdlet.ShouldProcess($game,'Remove AC7 DLSS Enhancer files')){return}
Update-AC7Files $game $files
Write-Output "Uninstalled AC7 DLSS Enhancer: $game"
