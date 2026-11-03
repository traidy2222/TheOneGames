param([switch]$Build,[switch]$Validate)
$ErrorActionPreference='Stop'
$gamesRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$exe=Join-Path $gamesRoot 'build/bin/tornado/Release/tornado.exe'
if($Build -or -not (Test-Path -LiteralPath $exe)) {
    & cmake -S $gamesRoot -B (Join-Path $gamesRoot 'build') -A x64
    if($LASTEXITCODE -ne 0){throw 'Tornado configuration failed'}
    & cmake --build (Join-Path $gamesRoot 'build') --config Release --target tornado --parallel 8
    if($LASTEXITCODE -ne 0){throw 'Tornado build failed'}
}
$launchArgs=@();if($Validate){
    $sdk=Get-ChildItem 'C:/VulkanSDK' -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1
    if($sdk){$env:VK_LAYER_PATH=Join-Path $sdk.FullName 'Bin'}
    $launchArgs+='--validate'
}
Push-Location (Split-Path $exe)
try { & $exe @launchArgs } finally { Pop-Location }
