# Build the engine and game for Windows as 32-bit (x86) with Visual Studio 2026.
# Uses the bundled 32-bit libraries, except AngelScript (built from source).
# Usage: .\build-windows.ps1 [-Config Release|Debug]
param([string]$Config = "Release")
$ErrorActionPreference = "Stop"

$Root  = $PSScriptRoot
$Deps  = Join-Path $Root "HPL2\dependencies"
$Build = Join-Path $Root "build-win32"

# CMake ships with Visual Studio; use it if cmake isn't on PATH
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
}

if (-not (Test-Path $Deps)) {
    Write-Host "Unpacking dependencies..."
    Expand-Archive (Join-Path $Root "HPL2\dependencies.zip") $Deps
}

& $cmake -P (Join-Path $Root "fetch-deps.cmake")
if ($LASTEXITCODE) { exit $LASTEXITCODE }

# CMAKE_POLICY_VERSION_MINIMUM lets CMake 4+ accept the project's old cmake_minimum_required(2.8.11)
& $cmake -S (Join-Path $Root "amnesia\src") -B $Build -G "Visual Studio 18 2026" -A Win32 -Wno-dev `
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
if ($LASTEXITCODE) { exit $LASTEXITCODE }

& $cmake --build $Build --config $Config -- -m
if ($LASTEXITCODE) { exit $LASTEXITCODE }

Write-Host "Built: $Build\$Config\Amnesia.exe"
