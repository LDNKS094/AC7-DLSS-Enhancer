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
# Fails if an existing file is locked or read-only, or if its folder (or nearest existing ancestor) is not writable.
function Assert-AC7Writable([string]$Path){
    if(Test-Path -LiteralPath $Path -PathType Leaf){
        try{[IO.File]::Open($Path,'Open','ReadWrite','None').Dispose()}
        catch{throw "Cannot modify ${Path}: in use or no permission. $($_.Exception.Message)"}
    }else{
        $dir=Split-Path -Parent $Path
        while(-not(Test-Path -LiteralPath $dir -PathType Container)){$dir=Split-Path -Parent $dir}
        $probe=Join-Path $dir ('.ac7-probe-'+[guid]::NewGuid().ToString('N'))
        try{[IO.File]::Open($probe,'CreateNew','Write','None').Dispose();Remove-Item -LiteralPath $probe -Force}
        catch{throw "Cannot create files in ${dir}: no permission. $($_.Exception.Message)"}
    }
}
# Removes $Path only if it is a directory inside the game folder and has no remaining entries.
function Remove-AC7EmptyDir([string]$Path,[string]$Game){
    if($Path -ne $Game -and (Test-Path -LiteralPath $Path -PathType Container) -and -not(Get-ChildItem -LiteralPath $Path -Force)){Remove-Item -LiteralPath $Path -Force}
}
# $Files: relative path => source file to copy, or $null to remove the target.
# All targets are checked before anything changes. If a copy fails, the files copied in this run are removed.
function Update-AC7Files([string]$Game,[System.Collections.IDictionary]$Files){
    foreach($relative in $Files.Keys){
        $target=Join-Path $Game $relative
        foreach($item in @($Game,(Split-Path -Parent $target),$target) | Select-Object -Unique){
            if((Test-Path -LiteralPath $item) -and ((Get-Item -LiteralPath $item -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw "Refusing linked install path: $item"}
        }
        Assert-AC7Writable $target
    }
    $copied=@()
    try{
        foreach($relative in $Files.Keys){
            $target=Join-Path $Game $relative
            if($null -eq $Files[$relative]){
                Remove-Item -LiteralPath $target -Force
                Remove-AC7EmptyDir (Split-Path -Parent $target) $Game
            }else{
                New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
                $copied+=,$target
                Copy-Item -LiteralPath $Files[$relative] -Destination $target -Force
            }
        }
    }catch{
        $failure=$_
        foreach($target in $copied){
            Remove-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue
            Remove-AC7EmptyDir (Split-Path -Parent $target) $Game
        }
        throw $failure
    }
}
