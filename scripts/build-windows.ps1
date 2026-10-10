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
    # Build-time projections only: no WinUI package/runtime is installed on the VDI.
    $deps = Join-Path $buildDir 'projection-deps'
    New-Item -ItemType Directory -Force $deps | Out-Null
    $archive = Join-Path $deps 'Microsoft.UI.Xaml.2.8.7.zip'
    if (!(Test-Path $archive)) {
        Invoke-WebRequest 'https://www.nuget.org/api/v2/package/Microsoft.UI.Xaml/2.8.7' -OutFile $archive
    }
    $expectedHash = '79207b10fe243eb1a8dcdc29beceba2f472f145ed31f147f7ae3f43b0659c9f7'
    if ((Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedHash) {
        throw 'Microsoft.UI.Xaml package hash mismatch'
    }
    $mux = Join-Path $deps 'mux'
    Expand-Archive $archive -DestinationPath $mux -Force
    # WinUI metadata references WebView2 even though TaskbarOrder uses no browser control.
    $webArchive = Join-Path $deps 'Microsoft.Web.WebView2.1.0.2849.39.zip'
    if (!(Test-Path $webArchive)) {
        Invoke-WebRequest 'https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2849.39' -OutFile $webArchive
    }
    if ((Get-FileHash $webArchive -Algorithm SHA256).Hash.ToLowerInvariant() -ne '099ec21f62e0e29ef9d9a991d096421efb256bdfdb64984216395736043560fd') {
        throw 'Microsoft.Web.WebView2 package hash mismatch'
    }
    $web = Join-Path $deps 'webview2'
    Expand-Archive $webArchive -DestinationPath $web -Force
    $sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
    $sdk = Get-ChildItem (Join-Path $sdkRoot 'bin') -Directory |
        Where-Object { $_.Name -match '^10\.0\.\d+\.\d+$' -and (Test-Path (Join-Path $_.FullName 'x64/cppwinrt.exe')) } |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if (!$sdk) { throw 'Windows SDK cppwinrt.exe not found' }
    $projection = Join-Path $deps 'include'
    # Generate OS and WinUI projections with the same generator/base.h version.
    & (Join-Path $sdk.FullName 'x64/cppwinrt.exe') -input $sdk.Name -input (Join-Path $mux 'lib/uap10.0/Microsoft.UI.Xaml.winmd') -input (Join-Path $web 'lib/Microsoft.Web.WebView2.Core.winmd') -output $projection
    if ($LASTEXITCODE -ne 0) { throw 'C++/WinRT projection generation failed' }
    & cmake -S . -B $buildDir -G $Generator -A x64 -DBUILD_TESTING=ON "-DCPPWINRT_INCLUDE_DIR=$projection"
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
    & cmake --build $buildDir --config $Configuration --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Windows compilation failed' }
    & ctest --test-dir $buildDir -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
    if (Test-Path $distDir) { Remove-Item $distDir -Recurse -Force }
    & cmake --install $buildDir --config $Configuration --prefix $distDir
    if ($LASTEXITCODE -ne 0) { throw 'Packaging failed' }
    Copy-Item (Join-Path $mux 'license.txt') (Join-Path $distDir 'licenses/Microsoft.UI.Xaml.txt')
    Copy-Item (Join-Path $web 'LICENSE.txt') (Join-Path $distDir 'licenses/Microsoft.Web.WebView2.txt')
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
