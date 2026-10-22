param(
    [Parameter(Mandatory=$true)][string]$VcpkgRoot,
    [string]$SdkRoot = "",
    [string]$BuildDirectory = "build-windows"
)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$toolchain = Join-Path $VcpkgRoot "scripts/buildsystems/vcpkg.cmake"
if (-not (Test-Path $toolchain)) { throw "VcpkgRoot must contain a bootstrapped vcpkg checkout" }
$build = Join-Path $repo $BuildDirectory
$cmakeArgs = @("-S", $repo, "-B", $build, "-G", "Visual Studio 17 2022", "-A", "x64",
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain", "-DVCPKG_OVERLAY_TRIPLETS=$repo/cmake/triplets",
    "-DVCPKG_TARGET_TRIPLET=x64-windows-release")
if ($SdkRoot) {
    $sdk = (Resolve-Path $SdkRoot).Path
    $cmakeArgs += @("-DQARVTK_BUILD_SENDER=ON", "-DQAROS_SDK_ROOT=$sdk")
} else {
    $cmakeArgs += "-DQARVTK_BUILD_SENDER=OFF"
}
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }
& cmake --build $build --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw "Build failed" }
& ctest --test-dir $build -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Tests failed; inspect the OpenGL driver and test output" }
Write-Output "Build and tests completed: $build/Release"
