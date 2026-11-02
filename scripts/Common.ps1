$ErrorActionPreference='Stop'
function Resolve-AC7Game([string]$GameDirectory){
    if($GameDirectory){
        $folder=[IO.Path]::GetFullPath($GameDirectory)
        if(-not(Test-Path -LiteralPath (Join-Path $folder 'Ace7Game.exe') -PathType Leaf)){throw 'Ace7Game.exe not found. Specify -GameDirectory.'}
        return $folder
    }
    $roots=@()
    foreach($key in @('HKCU:\Software\Valve\Steam','HKLM:\SOFTWARE\WOW6432Node\Valve\Steam')){
        $entry=Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
        if($entry.SteamPath){$roots+=,$entry.SteamPath}
        if($entry.InstallPath){$roots+=,$entry.InstallPath}
    }
    $libraries=@($roots)
    foreach($folder in $roots){
        $file=Join-Path $folder 'steamapps\libraryfolders.vdf'
        if(Test-Path -LiteralPath $file){
            foreach($match in [regex]::Matches([IO.File]::ReadAllText($file),'"path"\s*"([^"]+)"')){$libraries+=,$match.Groups[1].Value.Replace('\\','\')}
        }
    }
    $games=@($libraries | Select-Object -Unique | ForEach-Object {Join-Path $_ 'steamapps\common\ACE COMBAT 7'} | Where-Object {Test-Path -LiteralPath (Join-Path $_ 'Ace7Game.exe')})
    if($games.Count -ne 1){throw 'Could not select one Steam installation. Specify -GameDirectory.'}
    return [IO.Path]::GetFullPath($games[0])
}
function Assert-AC7Closed{
    if(Get-Process Ace7Game -ErrorAction SilentlyContinue){throw 'Close AC7 before installing or uninstalling.'}
}
function Get-AC7RuntimeNames{
    return @('nvngx_dlss.dll','nvngx_dlssnr.dll','sl.interposer.dll','sl.common.dll','sl.dlss_g.dll','sl.reflex.dll','sl.pcl.dll','nvngx_dlssg.dll')
}
function Write-AC7Utf8([string]$Path,[string]$Text){
    [IO.File]::WriteAllText($Path,$Text,(New-Object Text.UTF8Encoding($false)))
}
function Update-AC7Files([string]$Game,[System.Collections.IDictionary]$Files,[string]$Backup){
    $records=@()
    foreach($relative in $Files.Keys){
        $target=Join-Path $Game $relative
        $parent=Split-Path -Parent $target
        foreach($item in @($Game,$parent,$target) | Select-Object -Unique){
            if((Test-Path -LiteralPath $item) -and ((Get-Item -LiteralPath $item -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw "Refusing linked install path: $item"}
        }
        $existed=Test-Path -LiteralPath $target
        if($existed){
            if(-not(Test-Path -LiteralPath $target -PathType Leaf)){throw "Expected a file: $target"}
            $saved=Join-Path $Backup $relative
            New-Item -ItemType Directory -Path (Split-Path -Parent $saved) -Force | Out-Null
            Copy-Item -LiteralPath $target -Destination $saved
        }
        $records+=,[pscustomobject]@{Path=$relative;Existed=$existed}
    }
    $done=@()
    try{
        foreach($record in $records){
            $done+=,$record
            $target=Join-Path $Game $record.Path
            $source=$Files[$record.Path]
            if($null -eq $source){Remove-Item -LiteralPath $target -Force}
            else{
                New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
                Copy-Item -LiteralPath $source -Destination $target -Force
            }
        }
    }catch{
        $failure=$_
        for($i=$done.Count-1;$i -ge 0;$i--){
            $record=$done[$i];$target=Join-Path $Game $record.Path
            try{
                if($record.Existed){Copy-Item -LiteralPath (Join-Path $Backup $record.Path) -Destination $target -Force}
                elseif(Test-Path -LiteralPath $target){Remove-Item -LiteralPath $target -Force}
            }catch{Write-Warning "Restore failed for $target; backup: $Backup"}
        }
        throw $failure
    }
}
