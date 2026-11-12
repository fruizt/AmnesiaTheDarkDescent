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

# The bundled SDL2 (2.0.3-era) breaks fullscreen and HiDPI on current macOS.
# Replace its headers and library with a modern SDL2 built for x86_64.
SDL_VERSION=2.32.10
SDL_SHA256=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165
SDL_STAMP="$DEPS/.sdl-$SDL_VERSION"
if [ ! -f "$SDL_STAMP" ]; then
    echo "Building SDL $SDL_VERSION..."
    SDL_WORK="$BUILD/sdl"
    rm -rf "$SDL_WORK"
    mkdir -p "$SDL_WORK"
    curl -fsSL -o "$SDL_WORK/sdl.tar.gz" \
        "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL2-$SDL_VERSION.tar.gz"
    echo "$SDL_SHA256  $SDL_WORK/sdl.tar.gz" | shasum -a 256 -c -
    tar xzf "$SDL_WORK/sdl.tar.gz" -C "$SDL_WORK"
    cmake -S "$SDL_WORK/SDL2-$SDL_VERSION" -B "$SDL_WORK/build" -Wno-dev \
        -DCMAKE_OSX_ARCHITECTURES=x86_64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13 \
        -DCMAKE_BUILD_TYPE=Release \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF
    cmake --build "$SDL_WORK/build" -j"$(sysctl -n hw.ncpu)"
    cmake --install "$SDL_WORK/build" --prefix "$SDL_WORK/install"

    rm -rf "$DEPS/include/SDL2"
    cp -R "$SDL_WORK/install/include/SDL2" "$DEPS/include/SDL2"
    cp "$SDL_WORK/install/lib/libSDL2-2.0.0.dylib" "$DEPS/lib/macosx64/libSDL2.dylib"
    install_name_tool -id @rpath/libSDL2.dylib "$DEPS/lib/macosx64/libSDL2.dylib" 2>/dev/null
    codesign -f -s - "$DEPS/lib/macosx64/libSDL2.dylib"
    touch "$SDL_STAMP"
fi

# CMAKE_POLICY_VERSION_MINIMUM lets CMake 4+ accept the project's old cmake_minimum_required(2.8.11).
cmake -S "$ROOT/amnesia/src" -B "$BUILD" -Wno-dev \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu)"

echo "Built: $BUILD/Amnesia.app"
