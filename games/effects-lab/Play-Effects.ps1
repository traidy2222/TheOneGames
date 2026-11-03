param([switch]$Validate)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Push-Location $root
try {
    & cmake -S . -B build -A x64
    if($LASTEXITCODE -ne 0){throw 'Configuration failed'}
    & cmake --build build --config Release --target effects_lab --parallel 1 -- /p:CL_MPCount=1
    if($LASTEXITCODE -ne 0){throw 'Effects lab build failed'}
    $arguments=@()
    $previousLayerPath=$env:VK_LAYER_PATH
    $previousLayerEnables=$env:VK_LAYER_ENABLES
    if($Validate){
        $arguments+='--validate'
        $include=Get-Content build/CMakeCache.txt | Where-Object {$_ -like 'Vulkan_INCLUDE_DIR:PATH=*'} | Select-Object -First 1
        if($include){$env:VK_LAYER_PATH=Join-Path (Split-Path ($include -split '=',2)[1]) 'Bin'}
        $env:VK_LAYER_ENABLES='VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT'
    }
    Push-Location $PSScriptRoot
    try { & "$root/build/bin/effects-lab/Release/effects_lab.exe" @arguments } finally {$env:VK_LAYER_PATH=$previousLayerPath;$env:VK_LAYER_ENABLES=$previousLayerEnables;Pop-Location}
} finally {Pop-Location}
