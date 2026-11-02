param([string]$Configuration='Release',[string]$CMake='cmake')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$build=Join-Path $root 'build'
& $CMake -S $root -B $build -G 'Visual Studio 17 2022' -A x64
if($LASTEXITCODE){throw 'CMake configure failed.'}
& $CMake --build $build --target ac7-dlss --config $Configuration --parallel 4
if($LASTEXITCODE){throw 'ASI build failed.'}
$payload=Join-Path $root 'payload'
New-Item -ItemType Directory -Path (Join-Path $payload 'AC7DLSS') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $build "$Configuration\AC7DLSS.asi") -Destination $payload -Force
. (Join-Path $PSScriptRoot 'Common.ps1')
foreach($name in Get-AC7RuntimeNames){
    $source=if($name -in @('nvngx_dlss.dll','nvngx_dlssnr.dll')){Join-Path $root ('third_party\models\'+$name)}else{Join-Path $root ('third_party\streamline\bin\x64\'+$name)}
    Copy-Item -LiteralPath $source -Destination (Join-Path $payload 'AC7DLSS') -Force
}
Write-Output "Built AC7 DLSS Enhancer: $payload"
