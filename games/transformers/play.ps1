param([switch]$Build)
$ErrorActionPreference='Stop'
$gamesRoot=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$executable=Join-Path $gamesRoot 'build/bin/transformers-play/Release/transformers_play.exe'
if($Build -or !(Test-Path $executable)) {
    cmake -S $gamesRoot -B (Join-Path $gamesRoot 'build')
    if($LASTEXITCODE -ne 0){throw 'Configure failed'}
    cmake --build (Join-Path $gamesRoot 'build') --config Release --target transformers_play --parallel 6
    if($LASTEXITCODE -ne 0){throw 'Build failed'}
}
& $executable
exit $LASTEXITCODE
