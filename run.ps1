param([ValidateSet('Sandbox','Robots','Planets')][string]$Game='Planets', [switch]$Test, [ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    $sdkArguments=@()
    if (-not $env:VULKAN_SDK -and (Test-Path 'C:\VulkanSDK')) {
        $sdk=Get-ChildItem 'C:\VulkanSDK' -Directory | Sort-Object Name -Descending | Select-Object -First 1
        if($sdk) { $sdkArguments=@("-DVulkan_ROOT=$($sdk.FullName)") }
    }
    & cmake -S . -B build -A x64 @sdkArguments
    if($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
    if ($Test) {
        & cmake --build build --config $Configuration --parallel 8
        if($LASTEXITCODE -ne 0) { throw 'Build failed' }
        & ctest --test-dir build -C $Configuration --output-on-failure
        if($LASTEXITCODE -ne 0) { throw 'Tests failed' }
    } else {
        $target=$Game.ToLower()
        & cmake --build build --config $Configuration --target $target --parallel 8
        if($LASTEXITCODE -ne 0) { throw 'Build failed' }
        if($Game -eq 'Robots') { & './games/robots/Play-Robots.ps1' -Configuration $Configuration }
        else {
            $exe=Join-Path $PSScriptRoot "build/bin/$target/$Configuration/$target.exe"
            if($Game -eq 'Planets') { $exe=(Get-Content -LiteralPath (Join-Path $PSScriptRoot "build/launch/planets-$Configuration.txt") -Raw).Trim() }
            Push-Location (Join-Path $PSScriptRoot "games/$target")
            try { & $exe; if($LASTEXITCODE -ne 0) { throw "$Game exited with an error" } } finally { Pop-Location }
        }
    }
} finally { Pop-Location }
