param([switch]$Validate)
$ErrorActionPreference='Stop'
$gameRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Push-Location $gameRoot
try {
    & cmake -S . -B build -A x64
    if($LASTEXITCODE -ne 0){throw 'Configuration failed'}
    & cmake --build build --config Release --target stellar_sandbox --parallel 1 -- /p:CL_MPCount=1
    if($LASTEXITCODE -ne 0){throw 'Stellar sandbox build failed'}
    $arguments=@();$oldLayer=$env:VK_LAYER_PATH
    if($Validate){$arguments+='--validate';$include=Get-Content build/CMakeCache.txt | Where-Object {$_ -like 'Vulkan_INCLUDE_DIR:PATH=*'} | Select-Object -First 1;if($include){$env:VK_LAYER_PATH=Join-Path (Split-Path ($include -split '=',2)[1]) 'Bin'}}
    Push-Location $PSScriptRoot
    try { & "$gameRoot/build/bin/stellar-sandbox/Release/stellar_sandbox.exe" @arguments } finally {$env:VK_LAYER_PATH=$oldLayer;Pop-Location}
} finally {Pop-Location}
