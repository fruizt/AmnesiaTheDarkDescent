#!/bin/bash
# Build the engine, game and editors for macOS as x86_64.
# On Apple Silicon the result runs through Rosetta (the bundled libraries are Intel-only).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
DEPS="$ROOT/HPL2/dependencies"
BUILD="$ROOT/build"

if [ ! -d "$DEPS" ]; then
    echo "Unpacking dependencies..."
    unzip -q "$ROOT/HPL2/dependencies.zip" -d "$DEPS"
    # The zip stores symlinks as text files containing "link <target>"; turn them back into symlinks.
    { grep -rlI '^link ' "$DEPS/lib" || true; } | while read -r f; do
        target="$(sed 's/^link //' "$f")"
        rm "$f"
        ln -s "$target" "$f"
    done
fi

# CMAKE_POLICY_VERSION_MINIMUM lets CMake 4+ accept the project's old cmake_minimum_required(2.8.11).
cmake -S "$ROOT/amnesia/src" -B "$BUILD" -Wno-dev \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu)"

echo "Built: $BUILD/Amnesia.app"
