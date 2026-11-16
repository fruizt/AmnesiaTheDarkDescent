# Modernization plan and status

Last updated 2026-11-15.

## Where things stand
- **macOS build works** (x86_64, runs on Apple Silicon through Rosetta) and plays
  smoothly with the Steam data. Done so far:
  - FBX mesh loader removed from the build (FBX SDK 2012 is gone; shipped assets
    use Collada/MSH).
  - Fixes for modern clang/libc++ (pointer vs bool compare, address of a temporary
    matrix, non-const `operator==`, stale SDL 1.2 `main` wrapper in MshConverter).
  - Bundled SDL2 (2.0.3 era) replaced with SDL 2.32.10 built from source; this
    fixed fullscreen, HiDPI and Cmd-Tab. Deployment target raised to macOS 10.13.
  - `build/compile_commands.json` is exported and `.vscode/settings.json` points
    the C/C++ extension at it.
- **Windows build is not modernized.** Only Visual Studio 2010 projects
  (toolset `v100`, platform `Win32`) and **32-bit** prebuilt libraries in
  `HPL2/dependencies/lib/win32` (checked: newton, angelscript, SDL2, glew32,
  DevIL, libvorbis are all x86 32-bit). CMake has no Windows support
  (`HPL2/core/CMakeLists.txt` stops with "Unknown platform").

## Why 64-bit, and why the dependencies are the work
Ray tracing needs far more than a 32-bit process's 4 GB. The engine and game code
have no CPU-specific code, so the work is replacing the prebuilt libraries with
64-bit builds from source. The same upgrades serve Windows x64 and macOS arm64.

| Library | Bundled | Plan | Risk |
|---|---|---|---|
| SDL2 | 2.0.3-era | 2.32.10 from source (already done on macOS) | Trivial |
| ogg / vorbis / theora | old | Current source | Easy (not yet checked) |
| GLEW | old | Current source | Easy (not yet checked) |
| DevIL | 1.7.8 | 1.8.0 from source (needs libpng etc.) | Medium (not yet checked) |
| Newton | 2.00 | **2.36** source: <https://github.com/sigmaco/newton-v2.36> (`coreLibrary_200`) | Windows x64: builds and passes tests (1 patch); HPL2 call sites still to port |
| AngelScript | 2.19.2 | Windows x64: **same 2.19.2** from source. macOS arm64: **2.37.0 or newer** | Windows: Easy. Mac arm64: Hardest |
| FLTK | — | Only used by the Linux launcher | Skip |

### Newton findings
- Newton 3.14 and 4.00 (<https://github.com/JulioJerez/newton-dynamics>) build on
  macOS but are a different API: of the 90 Newton functions HPL2 calls, 3.14 (and
  4.00's 3.14 wrapper) has 63 unchanged, 16 changed, 11 missing.
- **Newton 2.36 has all 90**; 84 unchanged. Changes needed in HPL2's physics
  backend (`HPL2/core/sources/impl/*Newton*.cpp`):
  - `NewtonCreate()` no longer takes allocator functions.
  - `NewtonCreateBody` and `NewtonSceneCollisionCreateProxy` take an extra
    `const dFloat* matrix`.
  - `NewtonMaterialGetContactForce` and `NewtonMaterialGetContactPositionAndNormal`
    take an extra `NewtonBody*`.
  - `NewtonWorldForEachBodyInAABBDo` and its `NewtonBodyIterator` callback gained
    a `void* userData` parameter.
  - The other 12 callback types HPL2 passes are unchanged.
- **License:** every source file carries Julio Jerez's zlib license. The
  sigmaco mirror's README claims "study only, all rights reserved", but a
  redistributor can't narrow the author's zlib grant; the file headers govern.
- **Verified on Windows x64 2026-11-15** (MSVC 14.52, static lib with
  `_NEWTON_STATIC_LIB`), fetched by `fetch-deps.cmake` (core library only):
  - Build all of `core/`, `physics/`, `newton/` except `dgVehicleConstraint.cpp`
    (dead: includes a missing header). Newton's own VS2010 file lists are stale.
  - **Bug fixed by patch:** `dgMemory.h` detected 64-bit with `__x86_64__` only
    (GCC/Clang), so MSVC x64 used the 32-bit memory block size and corrupted the
    heap inside `NewtonCreate`. Now also checks `_M_X64`.
  - Test (scratch program, C API used like `PhysicsWorldNewton`): free fall
    matches 0.5·g·t² within 2 cm, box rests on the floor at the right height,
    contact callback reports normals and forces, ray cast and AABB query work.
    Passed for HPL2's three solver/friction settings and with 4 threads, in
    Release and Debug (no Newton asserts).
- Not yet verified: arm64, and physics feel vs 2.00 in the real game.

### AngelScript findings
- The game registers 262 script functions (`amnesia/src/game/LuxScriptHandler.cpp`
  → `cLowLevelSystemSDL::AddScriptFunc`) with the **native** calling convention
  (`asCALL_STDCALL`), so AngelScript must support the CPU natively.
- Upstream history of `as_callfunc_arm64.cpp`: Linux arm64 support 2020-08,
  Apple Silicon support 2024-02, first released in **2.37.0 (2024-07-21)**.
  2.35/2.36 would mis-handle calls on Apple Silicon.
- Not yet measured: API changes 2.19.2 → 2.38.0 in the parts HPL2 uses, and
  whether Amnesia's `.hps` level scripts still compile.
- **Windows x64 needs no upgrade.** The 2.19.2 source is still published
  (angelcode.com) and already has `as_callfunc_x64_msvc.cpp/.asm`. Its
  `angelscript.h` matches the bundled one except a 3-line Frictional patch
  (`return 0;` → `asSFuncPtr dummy; return dummy;`, around line 955).
- **Verified 2026-11-15:** 2.19.2 builds as x64 with MSVC 14.52 (VS 2026) and
  native calls work (`AS_X64_MSVC`, no `AS_MAX_PORTABILITY`). A test program
  using the engine's own `scriptstring.cpp`/`scripthelper.cpp` passed in Debug and
  Release: `asCALL_STDCALL` functions with every return type the game uses
  (`void`, `bool`, `float`, `int`, `string&`), 5–8 mixed int/float/double/bool/
  string arguments, global properties, the `THISCALL` message callback, and both
  `cSqScript::Run` paths. Needed one engine fix: `scriptstring.cpp` registers
  `resize` through a wrapper, since MSVC's `std::string::resize` has no
  one-argument overload. The `.asm` file must be compiled from a path inside the
  build tree (`ml64` won't create nested output folders).
- Third-party source is downloaded into the git-ignored `external/` by
  `cmake -P fetch-deps.cmake`, pinned by version and SHA-256.

## The road to ray tracing
1. **Modernize dependencies.** Investigate the risky two first: build Newton 2.36
   from source and measure the AngelScript upgrade.
2. **Cross-platform 64-bit builds** from one CMake setup: add Windows x64
   (Visual Studio 2022+) and macOS arm64. Expect modern-MSVC errors similar to
   the clang ones already fixed. Verify the game plays the same.
3. **Vulkan backend, no RT**, matching the OpenGL look. Rendering is behind
   interfaces, so the renderer and game code don't change. OpenGL classes to
   mirror: `cLowLevelGraphicsSDL` (`iLowLevelGraphics`, 88 pure virtuals),
   `cSDLTexture` (`iTexture`), `cFrameBufferGL` (`iFrameBuffer`),
   `cDepthStencilBufferGL` (`iDepthStencilBuffer`), `cVertexBufferOGL_VBO`
   (`iVertexBuffer`), `cGLSLProgram`/`cGLSLShader` (`iGpuProgram`/`iGpuShader`),
   `cOcclusionQueryOGL` (`iOcclusionQuery`). SDL keeps window/input; create the
   window with `SDL_WINDOW_VULKAN` and use `SDL_Vulkan_CreateSurface`.
   Hard parts: the interface is OpenGL-state-machine shaped (needs a pipeline
   cache), and the 28 GLSL 1.20 shaders (loaded from the game data's
   `core/shaders`) must be rewritten and compiled to SPIR-V.
   Keep the OpenGL backend working to compare side by side.
4. **Hybrid ray tracing on Windows** (`VK_KHR_acceleration_structure`,
   `VK_KHR_ray_query` / `VK_KHR_ray_tracing_pipeline`): RT shadows first, then
   AO (replacing SSAO), reflections, bounce lighting. Full path tracing would
   need material/art rework. DLSS later.

Mac note: MoltenVK 1.4.2 (2026-07) exposes no ray tracing extensions (a WIP PR
exists), and the M2 has no RT hardware (Apple added it with M3). The Vulkan
raster backend can be developed on the Mac; RT work happens on Windows.

## Known leads
- Log error at menu load: `Sampler aGoboMap does not exist, could not bind it to unit 5`.
- Log shows `ShaderModel 3: 0` / `ShaderModel 4: 0` on macOS's legacy GL 2.1
  context; unknown how much quality that disables.
- macOS launcher disabled (`Launcher_Prefix.pch` missing from the release).
- SDL 1.2 code paths (`#if SDL_VERSION_ATLEAST(2,0,0)` branches in ~20 files)
  are dead in every current build and could be removed.
