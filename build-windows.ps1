# Build the engine and game for Windows with Visual Studio 2026.
#   x64 (default): libraries from vcpkg (vcpkg.json), Newton 2.36 and
#                  AngelScript 2.19.2 from external/ (fetch-deps.cmake).
#   Win32:         the bundled 32-bit libraries (the original baseline),
#                  except AngelScript, built from external/.
# Usage: .\build-windows.ps1 [-Arch x64|Win32] [-Config Release|Debug]
param(
    [ValidateSet("x64", "Win32")][string]$Arch = "x64",
    [string]$Config = "Release"
)
$ErrorActionPreference = "Stop"

$Root  = $PSScriptRoot
$Deps  = Join-Path $Root "HPL2\dependencies"
$Build = Join-Path $Root $(if ($Arch -eq "x64") { "build-win64" } else { "build-win32" })

# CMake and vcpkg ship with Visual Studio
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) { $cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" }

# Still needed for 64-bit: OALWrapper's sources and the Cg headers
if (-not (Test-Path $Deps)) {
    Write-Host "Unpacking dependencies..."
    Expand-Archive (Join-Path $Root "HPL2\dependencies.zip") $Deps
}

& $cmake -P (Join-Path $Root "fetch-deps.cmake")
if ($LASTEXITCODE) { exit $LASTEXITCODE }

# CMAKE_POLICY_VERSION_MINIMUM lets CMake 4+ accept the project's old cmake_minimum_required(2.8.11).
# (Quoted: Windows PowerShell splits unquoted -Dx=3.5 at the dot.)
$cmakeArgs = @("-S", (Join-Path $Root "amnesia\src"), "-B", $Build, "-G", "Visual Studio 18 2026",
               "-A", $Arch, "-Wno-dev", "-DCMAKE_POLICY_VERSION_MINIMUM=3.5")
if ($Arch -eq "x64") {
    # vcpkg builds the libraries in vcpkg.json on first configure (several minutes)
    $cmakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$vs\VC\vcpkg\scripts\buildsystems\vcpkg.cmake",
                  "-DVCPKG_MANIFEST_DIR=$Root", "-DVCPKG_TARGET_TRIPLET=x64-windows"
}
& $cmake @cmakeArgs
if ($LASTEXITCODE) { exit $LASTEXITCODE }

& $cmake --build $Build --config $Config -- -m
if ($LASTEXITCODE) { exit $LASTEXITCODE }

Write-Host "Built: $Build\$Config\Amnesia.exe"
