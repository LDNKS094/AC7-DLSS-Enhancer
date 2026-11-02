param([string]$OutputDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'Common.ps1')
if(-not $OutputDirectory){$OutputDirectory=Join-Path $root 'releases'}
$cmake=[IO.File]::ReadAllText((Join-Path $root 'CMakeLists.txt'))
if($cmake -notmatch 'project\(AC7DLSSEnhancer VERSION (\d+\.\d+\.\d+)'){throw 'Project version is missing.'}
$version=$Matches[1]
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$archive=Join-Path $OutputDirectory "AC7-DLSS-Enhancer-$version.zip"
if(Test-Path -LiteralPath $archive){throw "Package already exists: $archive. Choose another output directory or update the version."}
$stage=Join-Path $root ('build\package-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage -Force | Out-Null
foreach($name in @('README.md','README_zh.md','LICENSE','Install.cmd','Uninstall.cmd','presets','licenses')){
    Copy-Item -LiteralPath (Join-Path $root $name) -Destination $stage -Recurse
}
New-Item -ItemType Directory -Path (Join-Path $stage 'scripts') -Force | Out-Null
foreach($name in @('Common.ps1','Install.ps1','Uninstall.ps1')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $stage 'scripts')}
New-Item -ItemType Directory -Path (Join-Path $stage 'payload\AC7DLSS') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'payload\AC7DLSS.asi') -Destination (Join-Path $stage 'payload')
foreach($name in Get-AC7RuntimeNames){
    Copy-Item -LiteralPath (Join-Path $root ('payload\AC7DLSS\'+$name)) -Destination (Join-Path $stage 'payload\AC7DLSS')
}
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive -CompressionLevel Optimal
Write-AC7Utf8 ($archive+'.sha256') ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash+'  '+(Split-Path -Leaf $archive)+"`n")
Write-Output "Package: $archive"
