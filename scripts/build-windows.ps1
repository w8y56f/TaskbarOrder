param(
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [string]$Generator = 'Visual Studio 17 2022'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$version = (Get-Content (Join-Path $projectRoot 'VERSION') -Raw).Trim()
$packageName = "TaskbarOrder-$version-win11-x64"
$buildDir = Join-Path $projectRoot 'build-windows'
$distDir = Join-Path $projectRoot "dist/$packageName"
Push-Location $projectRoot
try {
    & cmake -S . -B $buildDir -G $Generator -A x64 -DBUILD_TESTING=ON
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
    & cmake --build $buildDir --config $Configuration --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Windows compilation failed' }
    & ctest --test-dir $buildDir -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
    if (Test-Path $distDir) { Remove-Item $distDir -Recurse -Force }
    & cmake --install $buildDir --config $Configuration --prefix $distDir
    if ($LASTEXITCODE -ne 0) { throw 'Packaging failed' }
    # Include corresponding source alongside this GPL prototype distribution.
    $sourceDir = Join-Path $distDir 'source'
    New-Item -ItemType Directory -Force $sourceDir | Out-Null
    foreach ($item in @('VERSION', 'CMakeLists.txt', 'LICENSE', 'README.md', 'THIRD_PARTY_NOTICES.md', 'src', 'tests', 'scripts', 'docs', 'third_party')) {
        Copy-Item -Path (Join-Path $projectRoot $item) -Destination $sourceDir -Recurse -Force
    }
    $zip = Join-Path $projectRoot "dist/$packageName.zip"
    Compress-Archive -Path (Join-Path $distDir '*') -DestinationPath $zip -Force
    Write-Host "TaskbarOrder $version portable package: $zip"
    Write-Host 'Windows compilation does not validate Explorer integration. Follow docs/VALIDATION.md on Windows 11.'
} finally { Pop-Location }
