param([ValidateSet('optimus','optimusflight','optimusgold')][string]$Variant='optimus',[switch]$Build)
$ErrorActionPreference='Stop'
$gamesRoot=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$executable=Join-Path $gamesRoot 'build/bin/transformers/Release/transformers.exe'
if($Build -or !(Test-Path -LiteralPath $executable)) {
    cmake -S $gamesRoot -B (Join-Path $gamesRoot 'build')
    if($LASTEXITCODE -ne 0){throw 'Configure failed'}
    cmake --build (Join-Path $gamesRoot 'build') --config Release --target transformers --parallel 6
    if($LASTEXITCODE -ne 0){throw 'Build failed'}
}
$asset=Join-Path $PSScriptRoot "build/exported/$Variant/$Variant.json"
if(!(Test-Path -LiteralPath $asset)){throw "Run the local Optimus export first: $asset"}
& $executable --asset $asset
exit $LASTEXITCODE
