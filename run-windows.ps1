# Run the built game using the data from an installed copy of the game.
# Usage: .\run-windows.ps1 [-GameDir <path>] [-Config Release|Debug]
# The game's 32-bit DLLs are copied next to our exe; the game folder is only read.
param(
    [string]$GameDir = "F:\SteamLibrary\steamapps\common\Amnesia The Dark Descent",
    [string]$Config = "Release"
)
$ErrorActionPreference = "Stop"

$ExeDir = Join-Path $PSScriptRoot "build-win32\$Config"
$Exe = Join-Path $ExeDir "Amnesia.exe"
if (-not (Test-Path $Exe)) { throw "Not built yet: run .\build-windows.ps1 first" }
if (-not (Test-Path (Join-Path $GameDir "core"))) { throw "No game data in '$GameDir'; pass -GameDir" }

# The DLLs the exe links against (matching the bundled import libraries), plus
# their own dependencies and OpenAL's Creative backend.
$dlls = "Newton", "glew32", "libtheora", "DevIL", "zlibwapi", "SDL2",
        "libvorbisfile", "libvorbis", "libogg", "OpenAL32", "wrap_oal"
foreach ($dll in $dlls) {
    $dest = Join-Path $ExeDir "$dll.dll"
    if (-not (Test-Path $dest)) { Copy-Item (Join-Path $GameDir "$dll.dll") $dest }
}

# The engine uses the working directory as the game folder
Start-Process $Exe -WorkingDirectory $GameDir
