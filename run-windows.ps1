# Run the built game using the data from an installed copy of the game.
# Usage: .\run-windows.ps1 [-Arch x64|Win32] [-GameDir <path>] [-Config Release|Debug] [-Remix]
#   -Remix: enable the RTX Remix spike (x64; see docs/MODERNIZATION.md).
#           In game: F9 toggles classic/RTX, Alt+X opens Remix's menu in RTX mode.
# The game folder is only read, never written to.
param(
    [ValidateSet("x64", "Win32")][string]$Arch = "x64",
    [string]$GameDir = "F:\SteamLibrary\steamapps\common\Amnesia The Dark Descent",
    [string]$Config = "Release",
    [switch]$Remix
)
$ErrorActionPreference = "Stop"

$BuildDir = if ($Arch -eq "x64") { "build-win64" } else { "build-win32" }
$ExeDir = Join-Path $PSScriptRoot "$BuildDir\$Config"
$Exe = Join-Path $ExeDir "Amnesia.exe"
if (-not (Test-Path $Exe)) { throw "Not built yet: run .\build-windows.ps1 -Arch $Arch first" }
if (-not (Test-Path (Join-Path $GameDir "core"))) { throw "No game data in '$GameDir'; pass -GameDir" }

if ($Arch -eq "Win32") {
    # The 32-bit build links the bundled import libraries, whose DLLs ship with
    # the game: copy them next to our exe, plus their own dependencies and
    # OpenAL's Creative backend. (The x64 build gets its DLLs from vcpkg.)
    $dlls = "Newton", "glew32", "libtheora", "DevIL", "zlibwapi", "SDL2",
            "libvorbisfile", "libvorbis", "libogg", "OpenAL32", "wrap_oal"
    foreach ($dll in $dlls) {
        $dest = Join-Path $ExeDir "$dll.dll"
        if (-not (Test-Path $dest)) { Copy-Item (Join-Path $GameDir "$dll.dll") $dest }
    }
}

# The game inherits these environment variables
$env:HPL_REMIX = if ($Remix) { "1" } else { "0" }
# Remix settings from the repo (remix\rtx.conf), not the game folder
$env:DXVK_RTX_CONFIG_FILE = if ($Remix) { Join-Path $PSScriptRoot "remix\rtx.conf" } else { "" }

# The engine uses the working directory as the game folder
Start-Process $Exe -WorkingDirectory $GameDir
