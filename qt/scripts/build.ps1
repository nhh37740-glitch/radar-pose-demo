param([Parameter(Mandatory=$true)][string]$QtRoot)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$env:MSBUILDDISABLENODEREUSE='1'
& cmake -S $root -B "$root/build" -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$QtRoot"
if($LASTEXITCODE -ne 0){throw 'Qt configuration failed'}
& cmake --build "$root/build" --config Release --parallel 3
if($LASTEXITCODE -ne 0){throw 'Qt executable/module build failed'}
