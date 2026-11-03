param([ValidateSet('Village','City')][string]$Level='Village', [ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$gamesRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$exe=Join-Path $gamesRoot "build/bin/robots/$Configuration/robots.exe"
$scene=Join-Path $PSScriptRoot "build/robots-$($Level.ToLower()).one"
$music=Join-Path $PSScriptRoot "build/robots-converted/$($Level.ToLower())-music.wav"
if (!(Test-Path -LiteralPath $exe) -or !(Test-Path -LiteralPath $scene)) { throw 'Build the robots target first. See docs/ROBOTS_PORT.md for scene setup.' }
Push-Location $PSScriptRoot
try {
    $arguments=@('--scene',$scene)
    if($Level -eq 'City') { $arguments+=@('--spawn','-186.284','-2.54565','49.0127') }
    if(Test-Path -LiteralPath $music) { $arguments+=@('--music',$music) }
    & $exe @arguments
    if($LASTEXITCODE -ne 0) { throw "Robots exited with code $LASTEXITCODE" }
} finally { Pop-Location }
