param([Parameter(Mandatory=$true)][string]$QtRoot,[Parameter(Mandatory=$true)][string]$DataRoot,[switch]$AppOnly,[string]$Version='1.0.0')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$repo=Split-Path $root -Parent
$qt=(Resolve-Path -LiteralPath $QtRoot).Path
$data=(Resolve-Path -LiteralPath $DataRoot).Path
$count=(Get-Content -LiteralPath "$data/manifest.json" -Raw | ConvertFrom-Json).metadata.sampleCount
if(-not $AppOnly -and $count -ne 7203){throw 'Full desktop delivery requires the complete7203-frame data pack'}
$stage=Join-Path $root "build/package-$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())"
New-Item -ItemType Directory -Force -Path $stage | Out-Null
$env:MSBUILDDISABLENODEREUSE='1'
$env:PATH="$qt/bin;$env:PATH"
Write-Output 'Installing native executable and independent module SDK...'
& cmake --install "$root/build" --config Release --prefix "$stage/shared"
if($LASTEXITCODE -ne 0){throw 'Module installation failed'}
$runtime=Join-Path $stage 'shared/bin'
& "$qt/bin/windeployqt.exe" --release --no-translations --no-opengl-sw --no-system-d3d-compiler --compiler-runtime --dir $runtime "$runtime/radar-playback.exe"
if($LASTEXITCODE -ne 0){throw 'Qt dependency deployment failed'}
foreach($name in 'Core','Gui','Widgets','Network'){Copy-Item -LiteralPath "$qt/bin/Qt6$name.dll" -Destination $runtime -Force}
Copy-Item -LiteralPath "$qt/plugins/platforms/qoffscreen.dll" -Destination "$runtime/platforms" -Force
$vswhere="${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vsRoot=(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
$crt=Get-ChildItem -LiteralPath "$vsRoot/VC/Redist/MSVC" -Recurse -Directory | Where-Object {$_.FullName -match '\\x64\\Microsoft\.VC143\.CRT$' -and $_.FullName -notmatch 'onecore'} | Sort-Object FullName -Descending | Select-Object -First 1
if(-not $crt){throw 'MSVC runtime missing'}
Get-ChildItem -LiteralPath $crt.FullName -Filter '*.dll' | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $runtime -Force}
$program=Join-Path $stage 'programs/radar-playback'
New-Item -ItemType Directory -Force -Path $program | Out-Null
Get-ChildItem -LiteralPath $runtime | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $program -Recurse -Force}
$factories=@{data_reader='radar_create_reader';playback='radar_create_playback';frontend='radar_create_frontend'}
foreach($module in 'contracts','data_reader','playback','frontend'){
    $destination=Join-Path $stage "modules/$module"
    New-Item -ItemType Directory -Force -Path "$destination/bin","$destination/lib","$destination/include/radar" | Out-Null
    Copy-Item -LiteralPath "$runtime/radar_$module.dll" -Destination "$destination/bin"
    Copy-Item -LiteralPath "$root/build/lib/radar_$module.lib" -Destination "$destination/lib"
    Copy-Item -LiteralPath "$root/include/radar/contracts.h" -Destination "$destination/include/radar"
    $deps=@();if($module -ne 'contracts'){$deps=@('contracts')}
    @{module=$module;version=$Version;abi='Qt6.8-MSVC2022-x64-C++17';factory=$factories[$module];dependencies=$deps;runtime='../../shared/bin';binary="bin/radar_$module.dll";library="lib/radar_$module.lib";header='include/radar/contracts.h'} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$destination/module.json" -Encoding utf8
}
Copy-Item -LiteralPath "$root/README.md","$repo/web/data-license.html" -Destination $stage
Copy-Item -LiteralPath "$root/docs","$root/examples" -Destination $stage -Recurse
New-Item -ItemType Directory -Force -Path "$stage/licenses" | Out-Null
foreach($name in 'LGPL-3.0-only.txt','GPL-3.0-only.txt','Qt-GPL-exception-1.0.txt'){Invoke-WebRequest -Uri "https://raw.githubusercontent.com/qt/qtbase/v6.8.3/LICENSES/$name" -OutFile "$stage/licenses/$name"}
if(Test-Path -LiteralPath "$qt/sbom"){Copy-Item -LiteralPath "$qt/sbom" -Destination "$stage/licenses/qt-sbom" -Recurse}
@'
Qt 6.8.3 is dynamically linked and remains replaceable. Source and notices:
https://download.qt.io/archive/qt/6.8/6.8.3/single/
https://code.qt.io/cgit/qt/qtbase.git/tree/?h=v6.8.3
Qt/third-party component notices are included in qt-sbom and accompanying licenses.
Microsoft x64 VC143 redistributable runtime DLLs accompany the program.
Oxford recording data/derived poses retain the separate data-license.html terms.
'@ | Set-Content -LiteralPath "$stage/licenses/README.txt" -Encoding utf8
@'
Windows x64 native Qt radar recording replay. Launch radar-playback.exe;
choose a prepared data-pack directory containing manifest.json, chunks, radar, stereo.
Or run: radar-playback.exe --data "C:/path/to/full"
This program-only archive includes its DLL/runtime dependencies but no recording data.
Full7203-frame delivery is in the separate radar-qt-full ZIP. No model inference runs.
'@ | Set-Content -LiteralPath "$stage/PROGRAM-README.txt" -Encoding utf8
@'
$ErrorActionPreference='Stop'
& "$PSScriptRoot/programs/radar-playback/radar-playback.exe"
'@ | Set-Content -LiteralPath "$stage/start-radar.ps1" -Encoding utf8
if(-not $AppOnly){Write-Output 'Copying complete7203-frame recording...';Copy-Item -LiteralPath $data -Destination "$program/full" -Recurse; $runtimeData="$program/full"; $bundled=7203}
else {$runtimeData=$data;$bundled=0}
Write-Output 'Compiling external SDK consumer without module implementation sources...'
& cmake -S "$root/examples" -B "$root/build/sdk-consumer" -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$qt" "-DRADAR_SDK=$stage/shared/sdk"
if($LASTEXITCODE -ne 0){throw 'Delivered SDK example configuration failed'}
& cmake --build "$root/build/sdk-consumer" --config Release --parallel 2
if($LASTEXITCODE -ne 0){throw 'Delivered SDK example compilation failed'}
Copy-Item -LiteralPath "$root/build/sdk-consumer/Release/radar-sdk-consumer.exe" -Destination $runtime -Force
$previousPath=$env:PATH;$previousPlugin=$env:QT_PLUGIN_PATH;$previousQtDir=$env:QTDIR
try {
    $env:PATH="$runtime;$env:SystemRoot/System32";Remove-Item Env:QT_PLUGIN_PATH -ErrorAction SilentlyContinue;Remove-Item Env:QTDIR -ErrorAction SilentlyContinue
    & "$runtime/radar-sdk-consumer.exe" $runtime $runtimeData
    if($LASTEXITCODE -ne 0){throw 'Module DLL consumer failed'}
} finally {$env:PATH=$previousPath;$env:QT_PLUGIN_PATH=$previousPlugin;$env:QTDIR=$previousQtDir}
Write-Output 'Testing actual delivered program with SDK paths removed...'
& python "$root/tests/process_test.py" --bin $program --data $runtimeData --evidence "$root/build/delivery-evidence" --expected-count $count --isolated --full-scan
if($LASTEXITCODE -ne 0){throw 'Binary-only real-data gate failed'}
$evidence=Join-Path $stage 'test-evidence';New-Item -ItemType Directory -Force -Path $evidence | Out-Null
Copy-Item -LiteralPath "$root/build/module-tests.xml","$root/build/process-evidence/summary.json" -Destination $evidence
Copy-Item -LiteralPath "$root/build/delivery-evidence/summary.json" -Destination "$evidence/binary-only-summary.json"
$run=(Get-Content -LiteralPath "$root/build/delivery-evidence/summary.json" -Raw | ConvertFrom-Json).runDir
Copy-Item -LiteralPath "$run/test_seek_pages_last_frame_and_real_widget_screenshot/interface.png" -Destination "$evidence/interface.png"
Copy-Item -LiteralPath "$run/test_paused_program_does_not_spin_cpu/idle-cpu.json" -Destination $evidence
Copy-Item -LiteralPath "$run/test_decode_complete_recorded_sequence/report.json" -Destination "$evidence/full-decode.json"
Copy-Item -LiteralPath "$run/test_export_every_pose_and_error_matches_source/report.json" -Destination "$evidence/full-export.json"
@{passed=$true;modulesLoaded=3;usesModuleSources=$false;consumer='shared/bin/radar-sdk-consumer.exe'} | ConvertTo-Json | Set-Content -LiteralPath "$evidence/sdk-consumer.json" -Encoding utf8
$revision=(& git -C $repo rev-parse HEAD).Trim()
Write-Output 'Streaming binary/data manifest and release archives...'
& python "$PSScriptRoot/build_delivery.py" --root $stage --output "$root/dist" --version $Version --revision $revision --bundled-frames $bundled
if($LASTEXITCODE -ne 0){throw 'Delivery archive audit failed'}
Write-Output "Verified native Qt delivery: $stage"
