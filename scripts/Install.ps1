[CmdletBinding(SupportsShouldProcess=$true)]
param([string]$GameDirectory)
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
$backup=Join-Path $root ('backups\install-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
if(-not(Test-Path -LiteralPath (Join-Path $game 'AC7DLSS.ini'))){
    $legacy=Join-Path $game 'AC7NativeModels.ini'
    if(Test-Path -LiteralPath $legacy){
        $text=[IO.File]::ReadAllText($preset)
        $oldSections=@{SuperResolution='SR';NeuralRendering='NR';FrameGeneration='FG'}
        $oldKeys=@{QualityMode='Mode';EffectStrength='Intensity';ToneStrength='Tone';DetailStrength='Structure';Preset='Style';OptimizeEdges='PeripheralCompression';CenterAspectRatio='CenterAspect';EdgeQuality='EdgeScale';BlendWidth='TransitionWidth'}
        $old=@{};$section=''
        foreach($line in [IO.File]::ReadAllLines($legacy)){
            if($line -match '^\s*\[([^\]]+)\]\s*$'){$section=$Matches[1]}
            elseif($line -match '^\s*([^;#=][^=]*?)\s*=\s*(.*?)\s*$'){$old[$section+'.'+$Matches[1].Trim()]=$Matches[2]}
        }
        $section=''
        $converted=foreach($line in ($text -split '\r?\n')){
            if($line -match '^\[([^\]]+)\]$'){$section=$Matches[1]}
            if($line -match '^([^;=]+)='){
                $key=$Matches[1];$oldKey=if($oldKeys.ContainsKey($key)){$oldKeys[$key]}else{$key}
                $lookup=$oldSections[$section]+'.'+$oldKey
                if($old.ContainsKey($lookup)){$line=$key+'='+$old[$lookup]}
            }
            $line
        }
        $preset=Join-Path $backup 'AC7DLSS-migrated.ini'
        Write-AC7Utf8 $preset ($converted -join "`r`n")
    }
    $files['AC7DLSS.ini']=$preset
}
if(Test-Path -LiteralPath (Join-Path $game 'AC7NativeModels.asi')){$files['AC7NativeModels.asi']=$null}
Update-AC7Files $game $files $backup
New-Item -ItemType Directory -Path (Join-Path $root '.local-state') -Force | Out-Null
Write-AC7Utf8 (Join-Path $root '.local-state\game-directory.txt') $game
Write-Output "Installed AC7 DLSS Enhancer: $game"
Write-Output "Previous files: $backup"
Write-Output 'Existing settings, legacy models, logs, captures and other mods were preserved.'
