$ErrorActionPreference = 'Stop'
$engineRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../TheOne'))
$gamesRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$backupRoot = Join-Path $gamesRoot 'migration-backup'
if (Test-Path -LiteralPath $backupRoot) { throw 'Migration backup already exists; do not repeat migration.' }
New-Item -ItemType Directory -Path $backupRoot | Out-Null
$sourceFiles = & git -C $engineRoot ls-files --cached --others --exclude-standard
foreach ($relative in ($sourceFiles | Sort-Object -Unique)) {
    $source = Join-Path $engineRoot $relative
    if (Test-Path -LiteralPath $source -PathType Leaf) {
        $destination = Join-Path $backupRoot "source/$relative"
        New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination
    }
}
& git -C $engineRoot status --porcelain=v1 | Set-Content -LiteralPath (Join-Path $backupRoot 'original-status.txt')
& git -C $engineRoot diff --binary | Set-Content -LiteralPath (Join-Path $backupRoot 'original-work.patch')
$moves = [Collections.Generic.List[object]]::new()
function Move-Preserved([string]$relative, [string]$target) {
    $source = [IO.Path]::GetFullPath((Join-Path $engineRoot $relative))
    $destination = [IO.Path]::GetFullPath((Join-Path $gamesRoot $target))
    if (!$source.StartsWith($engineRoot + [IO.Path]::DirectorySeparatorChar) -or !$destination.StartsWith($gamesRoot + [IO.Path]::DirectorySeparatorChar)) { throw 'Path escaped migration roots' }
    if (!(Test-Path -LiteralPath $source)) { return }
    if (Test-Path -LiteralPath $destination) { throw "Destination exists: $destination" }
    New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
    Move-Item -LiteralPath $source -Destination $destination
    $moves.Add(@{source=$source; destination=$destination})
}
# Keep small, original content fixtures for engine-only content/physics tests.
New-Item -ItemType Directory -Force -Path (Join-Path $engineRoot 'tests/fixtures') | Out-Null
Copy-Item -LiteralPath (Join-Path $engineRoot 'assets') -Destination (Join-Path $engineRoot 'tests/fixtures') -Recurse
Copy-Item -LiteralPath (Join-Path $engineRoot 'tests/core_tests.cpp') -Destination (Join-Path $backupRoot 'original-core-tests.cpp')
foreach ($name in 'application.cpp','application.hpp','game.hpp','sandbox_game.cpp') { Move-Preserved "src/$name" "shared/import-host/$name" }
Move-Preserved 'src/main.cpp' 'games/sandbox/src/main.cpp'
Move-Preserved 'assets' 'games/sandbox/assets'
Move-Preserved 'saves' 'games/sandbox/saves'
Move-Preserved 'Play-Robots.ps1' 'games/robots/Play-Robots.ps1'
foreach ($file in (Get-ChildItem -LiteralPath (Join-Path $engineRoot 'tools') -File | Where-Object Name -Match 'robots')) { Move-Preserved "tools/$($file.Name)" "games/robots/tools/$($file.Name)" }
foreach ($file in (Get-ChildItem -LiteralPath (Join-Path $engineRoot 'tests') -File | Where-Object Name -Match 'robots')) { Move-Preserved "tests/$($file.Name)" "games/robots/tests/$($file.Name)" }
foreach ($file in (Get-ChildItem -LiteralPath (Join-Path $engineRoot 'docs') -File | Where-Object Name -Match 'ROBOTS')) { Move-Preserved "docs/$($file.Name)" "games/robots/docs/$($file.Name)" }
Move-Preserved 'docs/licenses/EUROCHEF-MIT.txt' 'games/robots/docs/licenses/EUROCHEF-MIT.txt'
foreach ($name in 'generate_fixtures.py','make_compatibility_scene.py','fetch_test_samples.py') { Move-Preserved "tools/$name" "games/sandbox/tools/$name" }
foreach ($file in (Get-ChildItem -LiteralPath (Join-Path $engineRoot 'build') | Where-Object { $_.Name -match '^(robots|eurochef|vgmstream|compare.motion|sphinx_filelist)' })) { Move-Preserved "build/$($file.Name)" "games/robots/build/$($file.Name)" }
Move-Preserved 'build/external' 'games/sandbox/build/external'
$moves | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $backupRoot 'moves.json')
Write-Output "Preserved source backup and moved $($moves.Count) entries."
