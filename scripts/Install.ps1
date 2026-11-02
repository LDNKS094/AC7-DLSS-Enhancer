[CmdletBinding(SupportsShouldProcess=$true)]
param([string]$GameDirectory)
trap { [Console]::Error.WriteLine('Install failed: ' + $_.Exception.Message); exit 1 }
. (Join-Path $PSScriptRoot 'Common.ps1')
$root=Split-Path -Parent $PSScriptRoot
$game=Resolve-AC7Game $GameDirectory
Assert-AC7Closed
$files=[ordered]@{'AC7DLSS.asi'=(Join-Path $root 'payload\AC7DLSS.asi')}
foreach($name in Get-AC7RuntimeNames){$files['AC7DLSS\'+$name]=Join-Path $root ('payload\AC7DLSS\'+$name)}
$preset=Join-Path $root 'presets\AC7DLSS.ini'
foreach($source in @($files.Values)+@($preset)){
    if(-not(Test-Path -LiteralPath $source -PathType Leaf)){throw "Missing package file: $source"}
}
if(-not $PSCmdlet.ShouldProcess($game,'Install AC7 DLSS Enhancer, preserving existing AC7DLSS.ini')){return}
if(-not(Test-Path -LiteralPath (Join-Path $game 'AC7DLSS.ini'))){$files['AC7DLSS.ini']=$preset}
Update-AC7Files $game $files
Write-Output "Installed AC7 DLSS Enhancer: $game"
Write-Output 'Existing AC7DLSS.ini settings were preserved.'
