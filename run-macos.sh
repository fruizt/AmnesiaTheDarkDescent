#!/bin/bash
# Run the built game using the data from an installed copy of the game.
# Usage: ./run-macos.sh [path-to-game-data]   (defaults to the Steam install)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
GAME_DIR="${1:-$HOME/Library/Application Support/Steam/steamapps/common/Amnesia The Dark Descent}"

cd "$GAME_DIR"
exec arch -x86_64 "$ROOT/build/Amnesia.app/Contents/MacOS/Amnesia" -cwd
