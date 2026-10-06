param([Parameter(Mandatory=$true)][string]$QtRoot,[Parameter(Mandatory=$true)][string]$DataRoot,[int]$ExpectedCount=7203)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$env:PATH="$QtRoot/bin;$env:PATH"
$env:QT_QPA_PLATFORM='offscreen'
$env:QT_QPA_FONTDIR="$env:SystemRoot/Fonts"
& ctest --test-dir "$root/build" -C Release --output-on-failure --output-junit "$root/build/module-tests.xml"
if($LASTEXITCODE -ne 0){throw 'Module tests failed'}
& python "$root/tests/process_test.py" --bin "$root/build/bin" --data $DataRoot --evidence "$root/build/process-evidence" --expected-count $ExpectedCount --full-scan
if($LASTEXITCODE -ne 0){throw 'Native recorded-data process tests failed'}
