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
- **Windows 32-bit build works** (2026-11-15, `win64` branch) and runs with the
  Steam data: `.\build-windows.ps1`, then `.\run-windows.ps1`. This is the
  baseline before the 64-bit move. CMake gained Windows support (VS 2026,
  `-A Win32`), using the original VS project's platform files (`PlatformWin32`,
  `MutexWin32`, `ThreadWin32`, `GamepadXInput`) and the bundled 32-bit libraries
  in `HPL2/dependencies/lib/win32`, except AngelScript 2.19.2, built from
  `external/` (the VS2010 `angelscript.lib` needs the old C runtime's
  `_vsnprintf`). The game's own 32-bit DLLs are copied from the Steam folder.
  Code fixes for modern MSVC: `std::binary_function` removed from `VideoComp`,
  `const` comparators in `Renderer.h`/`GuiSet.h`, `<windows.h>` included before
  the old `SDL_syswm.h` (packing static assert). Editors and launcher are not
  built on Windows yet. Log: OpenGL 4.6 on the RTX 4070 Ti Super with Shader
  Models 2/3/4 all available (the Mac's GL 2.1 reported 3 and 4 as 0).
- **Windows 64-bit build works** (2026-11-15, `win64` branch): `.\build-windows.ps1`
  and `.\run-windows.ps1` (x64 is the default; `-Arch Win32` for the baseline).
  Reaches "Game Running" on the Steam data. Libraries:
  - vcpkg manifest `vcpkg.json` (repo root; vcpkg ships with VS): SDL2 2.32.10,
    ogg/vorbis/theora, GLEW, OpenAL Soft, zlib, DevIL 1.8.0 (only `libjpeg` and
    `libpng` features: game textures are DDS/TGA plus some JPG and one PNG).
    vcpkg's toolchain copies the DLLs next to the exe.
  - OpenAL Soft pinned to 1.24.3 (+ fmt 11.2.0 for it): 1.25.2 doesn't compile
    with MSVC 19.52 (vcpkg issue #54109). Drop the overrides once fixed.
  - Newton 2.36 and AngelScript 2.19.2 (+ its x64 MASM file) built from
    `external/`. The engine's Newton calls use `#if NEWTON_MINOR_VERSION >= 36`
    so the 2.00 builds (Mac, Win32) keep working. Contact force/normal queries
    pass body0, which gives 2.00's unflipped values.
  - The old `HPL2/dependencies/include` is not searched; only its Cg headers are
    copied to the build (a type name in `LowLevelGraphicsSDL.h`; Cg is only used
    under `WITH_CG`). `#pragma comment(lib)` names of the old libs are ignored with
    `/NODEFAULTLIB`. `FindDevIL` picks the debug lib, so it's set per config.
  - Only pointer-truncation warning was `ShellExecute`'s status code (fixed).
  - Log: DevIL 1.8.0 (the 1.6.8 cube map warning is gone), OpenAL Soft devices.
  - **Map caches:** `.map_cache` files hold serialized Newton collision meshes.
    The shipped ones are Newton 2.00's format; 2.36 misread them and crashed in
    `dgCollisionConvexPolygon::QuickTest` on the first player move. Builds with
    Newton 2.36 (`HPL_NEWTON_2_36`, set by the `newton` target) use
    `.map_cache_n236` instead, so the original game's caches are never touched.
    The game config defaults to `ForceCacheLoadingAndSkipSaving=true`, so no
    cache is written: levels build from source (Rainy Hall: 792 ms vs 581 ms
    cached). For a release we could ship or allow writing `_n236` caches.
  - Loads a save made by the 32-bit build and plays.
- Windows builds have a crash handler (`hplCrashHandler`, `LowLevelSystemSDL.cpp`)
  that writes the call stack to `hpl.log`; Release builds get a `.pdb` (`/Zi`,
  `/DEBUG`) so it has function names and lines.

## Why 64-bit, and why the dependencies are the work
Ray tracing needs far more than a 32-bit process's 4 GB. The engine and game code
have no CPU-specific code, so the work is replacing the prebuilt libraries with
64-bit builds from source. The same upgrades serve Windows x64 and macOS arm64.

| Library | Bundled | Plan | Risk |
|---|---|---|---|
| SDL2 | 2.0.3-era | 2.32.10 (macOS: from source; Windows x64: vcpkg) | Done |
| ogg / vorbis / theora | old | vcpkg (Windows x64) | Done on Windows |
| GLEW | old | vcpkg (Windows x64) | Done on Windows |
| OpenAL | Creative 32-bit | OpenAL Soft 1.24.3, vcpkg (Windows x64) | Done on Windows |
| DevIL | 1.7.8 | 1.8.0, vcpkg (Windows x64) | Done on Windows |
| Newton | 2.00 | **2.36** source: <https://github.com/sigmaco/newton-v2.36> (`coreLibrary_200`) | Done on Windows x64 (1 patch, call sites ported); physics feel still to check |
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
1. **Modernize dependencies.** Done for Windows x64 (vcpkg + `external/`).
2. **Cross-platform 64-bit builds** from one CMake setup. Windows x64 done
   (2026-11-15). Still to do: macOS arm64 (needs AngelScript 2.37+ and the same
   vcpkg-style libraries), and a longer play-through to confirm physics.
3. **RTX through RTX Remix (current top priority).** See "Decisions" below.
   First a spike to find out whether it works at all.

Deferred (only if we want our own vendor-neutral RT later, e.g. official
AMD/Intel support or lighter hybrid effects for mid-range cards):

4. **Vulkan backend, no RT**, matching the OpenGL look. Rendering is behind
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
5. **Hybrid ray tracing on Windows** (`VK_KHR_acceleration_structure`,
   `VK_KHR_ray_query` / `VK_KHR_ray_tracing_pipeline`): RT shadows first, then
   AO (replacing SSAO), reflections, bounce lighting. Full path tracing would
   need material/art rework. DLSS later.

Mac note: MoltenVK 1.4.2 (2026-07) exposes no ray tracing extensions (a WIP PR
exists), and the M2 has no RT hardware (Apple added it with M3). The Vulkan
raster backend can be developed on the Mac; RT work happens on Windows.

## Decisions (2026-11-15)
The goal is a **visual remaster**: path-traced lighting plus better assets.
- **RTX mode through the RTX Remix API**, not our own ray tracer, if the spike
  shows it works. Remix's capture mode can't be used (it intercepts DirectX 9;
  Amnesia is OpenGL with GLSL shaders and a deferred renderer). The **Remix API**
  lets an engine hand Remix its meshes, materials, lights and camera directly.
  Plan: a `cRendererRemix` beside `cRendererDeferred` (scene-level `iRenderer`).
  Remix's runtime is 64-bit, which the Windows x64 build now provides.
- **Classic mode stays**: the OpenGL renderer, for players without RTX. Remix
  officially needs an NVIDIA RTX card (recommended RTX 4070+); AMD RX 6000+
  runs it unofficially and slower; non-RT cards can't. Renderer chosen in settings.
- **Assets ship through the engine, not only through Remix**: upscaled/new
  textures (DDS with mipmaps) in a mod folder searched before the originals
  (`resources.cfg`), so classic mode benefits too. Classic mode uses diffuse,
  normal, specular and height maps; PBR-only maps (roughness, metalness) show in
  RTX mode. Remix's AI texture tools are one way to make them, not a dependency.
  Needs a texture-quality setting for low-VRAM cards (Amnesia has one).
- **Mod, not a replacement install**: our exe and files live in the game folder
  beside the original and never modify its files (e.g. `.map_cache_n236`), so
  the original game keeps working. Requires an owned copy; original assets are
  Frictional's (ask them before a public release of derived textures).
- **No Vulkan or DirectX 12 for now.** If ever needed: Vulkan preferred (Linux and
  Steam Deck native, Mac raster via MoltenVK; HLSL usable via DXC). DX12 is a bit
  less verbose with better Windows tooling (PIX); both are explicit pipeline APIs.
- **Stay on SDL2.** SDL3 brings nothing needed for RTX (its GPU API has no ray
  tracing); migrate later if useful.
- Other remaster areas (engine work, unaffected by the renderer choice): physics
  feel (Newton 2.36), animations (new `.dae` assets), new content (needs the
  editors built on Windows), HRTF audio (OpenAL Soft supports it), widescreen UI.

Next steps, riskiest first:
1. **Remix API spike**: one level path-traced with a fly-through camera, no
   gameplay or menus. Unknowns: sharing the window with OpenGL (presentation),
   skinned/animated meshes, particles/decals/fog, material conversion quality.
   See "Remix API findings" below for the phases.
2. **Texture override test**: a few upscaled textures in a mod folder, confirmed
   in classic mode.

## Remix API findings (2026-11-15)
Source: <https://github.com/NVIDIAGameWorks/dxvk-remix> (zlib, active), docs
`documentation/RemixSDK.md`, header `public/include/remix/remix_c.h` (C, plus a
C++ wrapper `remix.h`), examples `tests/rtx/apps/RemixAPI_C/remixapi_example_c.c`
and `tests/rtx/apps/RemixAPI/remixapi_skinning_test.cpp`.
- Works for any 64-bit app without D3D9: `remixapi_lib_loadRemixDllAndInitialize`
  (explicit DLL loading, no link changes), `Startup` with an `HWND`, register
  meshes/materials/lights, then per frame `SetupCamera`, `DrawInstance`,
  `DrawLightInstance`, `Present`. Marked "under development"; mostly tested by
  the Remix Toolkit's own renderer (the main risk).
- Prebuilt runtime: release **remix-1.6.1** (2026-10-23),
  `rtx-remix-for-x64-games-release-remix-1.6.1.zip` (531 MB), API **0.6.5**. Use
  the header from the `remix-1.6.1` tag (apps built against 0.6.x need 0.6.y
  binaries).
- Mapping to Amnesia:
  - Vertex format is fixed (`remixapi_HardcodedVertex`: position, normal, uv,
    color); a mesh is surfaces of triangles, each with a material.
  - Materials take **file paths**: albedo, normal, tangent, emissive, height,
    roughness, metallic (+ constants). Amnesia's diffuse/normal/height map
    directly; its specular maps need converting to roughness (or constants).
    Unknown: whether Remix loads Amnesia's DXT `.dds` and `.tga` files as-is.
  - Point lights = sphere lights; spot lights = sphere + `shaping` (cone angle,
    softness). No light gobo (projected texture) found.
  - Skinning: per-vertex bone weights/indices on the mesh, bone transforms per
    instance (`InstanceInfoBoneTransformsEXT`).
  - Presentation: `StartupInfo.forceNoVkSwapchain` + `dxvk_GetExternalSwapchain`
    hand the frame over as a `VkImage`, meant for presenting through OpenGL
    interop, so our OpenGL menus/HUD could draw on top.

Spike phases:
- **Phase 0, runtime check** (small): fetch the 1.6.1 x64 package and header
  into `external/`, build the official C triangle example, see it path-traced
  on this PC.
- **Phase 1, static level**: load a map with the engine, convert static meshes,
  materials (diffuse/normal only) and lights to Remix, fly a camera through it.
  Present to a separate window first to avoid the OpenGL interop question.
- **Phase 2, decide**: measure frame rate at 1440p with DLSS, judge the look,
  then try OpenGL interop presentation, one animated enemy, and material
  conversion. Go/no-go for `cRendererRemix`.

## Remix spike status (2026-11-16, `rtx-remix` branch)
- **Phase 0 done:** NVIDIA's C example renders path-traced through the API on
  the RTX 4070 Ti Super (Shader Execution Reordering on; DLSS-NR unavailable).
  `fetch-deps.cmake` pins the runtime zip and the SDK header/example files.
- **Phase 1 running:** `cRemixSpike` (`HPL2/core/sources/impl/RemixSpike.cpp`),
  hooked in `cScene::Render` after the world is drawn, mirrors the renderer's
  current render list (solid + translucent objects, point/spot lights, camera)
  into Remix in a second window. Built when `HPL_REMIX` is on (64-bit Windows),
  active with `.\run-windows.ps1 -Remix`. Amnesia geometry and textures render;
  translucent/alpha-test materials map to Remix blend/alpha-test modes.
- Lessons:
  - **Smart App Control** blocks Remix (its USD DLLs are unsigned). It's off on
    the dev PC; a public release would hit this for players who have it on.
  - The exe must sit **next to Remix's DLLs** (the shim loads dependencies from
    the application folder). CMake hard-links the runtime into the build output.
  - Remix writes `rtx-remix\` (logs), `<exe>.dxvk-cache`, `metrics.txt` and
    `remix-shim.log` into the **working directory** (the game folder).
  - **Keyboard died with Remix on:** Remix's default `rtx.useNewGuiInputMethod`
    creates an overlay that registers the keyboard as raw input with
    `RIDEV_NOLEGACY`, stopping `WM_KEYDOWN` for the whole process (mouse kept
    working). Fixed in `remix/rtx.conf` (startup-only option), which
    `run-windows.ps1 -Remix` passes via `DXVK_RTX_CONFIG_FILE`. Also set from
    code: `rtx.showUI=0`, `rtx.blockInputToGameInUI=False`.
  - Remix's first-use popup saves "allow crash reports / usage tracking" per
    game folder (`%LOCALAPPDATA%\NVIDIA\RTXRemix\user_settings\<hash>.conf`).
    Turned off for our folders.
- Open visual issues: most of a room can be missing or black (geometry or light
  level?), candle flames show when the candles are unlit (per-object
  illumination not passed), lights are dim (`HPL_REMIX_LIGHT_SCALE`), no normal
  maps, animation frozen, no particles/decals yet.
- **2026-11-16 update:**
  - Missing walls and wrong textures were a **stale cache**: the caches are keyed
    by engine pointers, and a new world reuses the old world's addresses.
    `cScene::DestroyWorld` now clears them.
  - Light-ray billboards (`bb_*_lightray`, 2x10 m fake light shafts) are skipped.
  - Remix now presents into the game window, and **F9** toggles classic/RTX.
    RTX mode plays fine with `rtx.blockInputToGameInUI = False`, set in
    `remix/rtx.conf`.
  - **RTX → classic doesn't work:** once Remix's Vulkan swapchain owns the window,
    OpenGL's `SwapBuffers` no longer reaches it, so the window keeps showing
    Remix's last frame while the game runs on.
  - Alt+X doesn't open Remix's menu, and Remix logs
    `[D3D9WindowProc] Swapchain handle is invalid`.
  - Still open: a see-through "veil" that keeps a fixed distance from the camera
    (likely something attached to the player), missing particles, Remix
    rejecting some particle textures ("Unsupported image file format").
- **Verdict:** path tracing Amnesia through the API works without porting the
  engine to Vulkan, since Remix's Vulkan stays inside Remix. The remaining doubts
  are presentation (below), frame rate and skinned characters.

## RTX mode design (2026-11-16)
Decision record: [0001](decisions/0001-remix-frame-shown-through-opengl.md).
Build steps 1 and 2 are done (commit `579c275`): the import works on the RTX
4070 Ti Super, and RTX mode shows in the game window with the HUD and post
effects on top, F9 working both ways.

**Step 3, frame rate (2026-11-16):** Rainy Hall, RTX 4070 Ti Super, the spike's
"Remix perf" log lines (5-second averages, CPU wall clock). Both modes hold a
steady 60 FPS, the cap of the 60 Hz game logic (`LimitFPS="true"`):

| | 1600x900 | 2560x1440 fullscreen |
|---|---|---|
| Classic, GL world | ~1–2 ms | ~0.6–1.2 ms |
| RTX, Remix (copy+wait) | ~6–7 ms | ~10–10.5 ms |
| RTX, GL world + scene + present + draw | ~1–2.5 ms | ~1–1.5 ms |
| RTX, uncapped | ~100 FPS | not measured |

- Remix is most of the RTX cost, and grows ~1.5x for 2.6x the pixels (fixed
  costs, maybe upscaling; Remix's upscaler setting wasn't checked).
- The first frames after switching to RTX take ~200–250 ms once (Remix building
  its acceleration structures and loading textures).
- `LimitFPS="false"` makes the classic image glitch (parts look frozen at ~1000
  FPS while the logic runs at 60 Hz). That's an engine trait, not Remix: keep the
  cap on.
- Not covered yet: bigger levels, many lights, particles, skinned characters,
  and the real cost of the OpenGL world render on the GPU in RTX mode.

**`cRendererRemix` (decision [0002](decisions/0002-renderer-remix.md)), 2026-11-16:**
- Build steps 1 and 2 are done. `cRemixContext` owns Remix and the interop,
  `cRemixScene` translates meshes, materials and lights, and `cRendererRemix`
  (`eRenderer_Remix`) draws the first world viewport of each frame in RTX
  mode. The spike is deleted.
- Remix gets everything within 50 m (`HPL_REMIX_RADIUS`), not just what's on
  screen. In Rainy Hall that's ~700 objects and ~70 lights, versus ~140 objects
  before. Gathering plus handing the scene over takes ~0.6 ms, Remix itself
  7.5–8.8 ms, and it holds 60 FPS at 1600x900. Old Archives: ~790 objects,
  33–45 lights, similar timings. The OpenGL world render no longer runs in RTX
  mode.
- RTX mode works in the main menu's 3D background too.
- Seen in play: the "veil" is still there, and **the player's hands don't show**
  in RTX mode.
- **Veil and hands solved (same cause):** Remix kept the first shape of every
  vertex buffer the engine rewrites. The veil was the area fog particles
  (`ps_area_fog`, a ~9x10 m emitter around the player) frozen at the moment
  RTX mode started. The hands (CPU-skinned) were frozen in their first pose.
  Objects with a non-static vertex buffer (particles, skinned meshes,
  billboards, beams, ropes) now get a new Remix mesh every frame, using only
  the indices drawn and the vertex colors. This replaces "Remix's skinning" in
  decision 0002's build step 3 for now. Remix skinning stays an option if
  enemies prove expensive. `HPL_REMIX_DIAG=1` logs the objects near the camera.
- Remix rejects **`.tga` and `.jpg` textures** ("Unsupported image file
  format"), so particles render untextured (the area fog shows as solid sheets
  on the floor).
- **Lights are uncalibrated:** inverse-square falloff blows out what's close to
  a light. The hand-held lantern makes the hands glow white.
- **Light calibration (first pass):** classic mode lights a surface with
  `color * (1 - d²/R²)` (linear falloff map), never brighter than the color.
  Remix lights now get `I = 0.1875 * color * R²`, matching classic at half the
  radius, from a 3–15 cm sphere. Lights within 1 m of the camera (the lantern,
  R = 10 m) are matched at 1 m instead, with a 0.5 m emitter. Auto exposure is
  off (`remix/rtx.conf`): it brightened dark rooms like a camera adjusting.
  Area fog particle cards are left out of RTX mode (they read as textured
  sheets), and a first volumetric air setting is in `rtx.conf`.
- **Next, grounded order (2026-11-16):** first finish translating Amnesia's
  lighting into Remix, checked against the data. The missing pieces are box
  lights (ambient fill, skipped so far), sky and outdoor light through
  openings, emissive materials, the trail and decals, and per-level fog. Then
  make Remix's Alt+X menu usable, so the look is tuned live instead of by
  guessing values. (Order changed: Alt+X first, since it also speeds up
  checking the translation.)
- **Alt+X works (2026-11-16):** in RTX mode it opens Remix's advanced menu over
  the game. `cRemixContext` subclasses the game window: Alt+X is caught there,
  and while the menu is open, mouse and keyboard messages go to the hidden
  window (Remix's input hook) and not to the game; the mouse lock is
  released. With `rtx.gui.enableExternalPresenter`, `CopyRenderingOutput(GUI)`
  draws the menu over the frame in the shared texture. The hidden window's
  `WM_MOUSELEAVE` is dropped (the mouse is never over it, so ImGui forgot the
  cursor), and click captures stay on the game window. "Save Settings" writes
  developer options to `remix/rtx.conf` (via `DXVK_RTX_CONFIG_FILE`, so changes
  show in git; it rewrites the file and drops comments) and user options to
  `user.conf` in the working directory (the game folder).
- **Fog from level data:** `cRendererRemix::UpdateFog` sets Remix's air from each
  level's fog. It works but changes little, which led to decision
  [0003](decisions/0003-rtx-look-real-light-sources.md): RTX mode is lit by
  real light sources, and the look is tuned with Remix's tools.
- **Stable hashes (0003 step 1, verified):** materials hash their file path
  relative to the game folder, static meshes their geometry (order-independent:
  the engine's combined static objects come out in a different triangle order
  every run, and position's w isn't always initialized) plus material, dynamic
  meshes and lights hash level + name. With `HPL_REMIX_DIAG=1` every hash is
  logged once. Two runs each of the menu (49 hashes) and Rainy Hall (519:
  308 meshes, 138 materials, 40 lights, 33 dynamic) gave identical hashes.
- **Amnesia's own maps (0003 step 2):** Remix decodes normal maps as unsigned
  octahedral, so Amnesia's RGB normal maps are converted (98 in Rainy Hall), and
  specular maps become roughness (R intensity, G Blinn-Phong power). Materials
  without a specular map are fully rough. Surfaces gained relief, but the
  lantern turned out to dominate how scenes look.
- **The lantern (0003 step 4):** measured with `HPL_REMIX_DIAG`, its 10 m light
  sits 10–20 cm *behind* the lantern, inside the forearm (it's placed relative
  to the hands' origin; classic mode can't show the difference). Held lights,
  those of the `PlayerHands_` entity, now move to the center of the held
  glow-mapped object (the lantern), and the lantern's 0.3 m "hand glow" light
  is left out. It lights the room at full classic-matched strength. Up close it
  looks like the sun: brightness and reach are linked for a physical light,
  so the balance is an art decision. Drifting patchy fog was removed from
  `rtx.conf` (it showed as dark smoke in lantern light).

The real build, replacing the spike step by step. **OpenGL always owns the game
window.** Remix renders off-screen, OpenGL shows Remix's image where the 3D world
would be, then draws its post effects, menus and HUD on top as usual.

### Presentation: Remix's frame as an OpenGL texture
Researched in the dxvk-remix source (`rtx_remix_api.cpp`, `d3d9_swapchain.h`,
`d3d9_common_texture.cpp`, `dxvk_image.cpp`):
- `dxvk_GetExternalSwapchain` is **not usable** for us: it returns raw
  `VkImage`/`VkSemaphore` handles of Remix's own Vulkan device, with no
  exportable memory, so OpenGL can't read them.
- **Usable route:** a D3D9 texture created on Remix's device with a shared handle
  (`D3DPOOL_DEFAULT`, `D3DUSAGE_RENDERTARGET`, `pSharedHandle`) is backed by
  Vulkan memory exported as `VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT`.
  OpenGL imports that with `GL_EXT_memory_object` + `GL_EXT_memory_object_win32`
  (`glImportMemoryWin32HandleEXT(..., GL_HANDLE_TYPE_OPAQUE_WIN32_KMT_EXT, handle)`,
  then `glTexStorageMem2DEXT`). Each frame, `dxvk_CopyRenderingOutput(surface,
  FINAL_COLOR)` copies Remix's final image into that texture.
- That needs Remix's `IDirect3DDevice9Ex`, which `remixapi_Startup` keeps
  private. So we do what `Startup` does ourselves: `dxvk_CreateD3D9`, then
  `CreateDeviceEx`, then `dxvk_RegisterD3D9Device`. Remix's own swapchain is
  pointed at a **hidden window**, so it never touches the game window. (The
  public `dxvk_CreateD3D9` only skips the Vulkan swapchain together with editor
  mode, which changes other defaults; the hidden window avoids that.)

### Frame flow in RTX mode
In `cScene::Render`, for the main viewport:
1. Build the scene for Remix (first the spike's render list, later
   `cRendererRemix`), call Remix's `Present`, then `dxvk_CopyRenderingOutput`
   into the shared texture.
2. Instead of the OpenGL world render, draw the shared texture full-screen into
   the frame buffer the world would have gone to.
3. Post effects, 3D GUI and screen GUI (menus, HUD, notes, inventory) run
   unchanged on top. Amnesia's sanity and bloom effects would apply to the RTX
   image too.
4. OpenGL's `SwapBuffers` presents as always.

F9 picks between steps 1–2 and the normal OpenGL world render. Switching is
instant, and the main menu and HUD work in both modes.

### Open questions, in the order we'll answer them
1. **Synchronization:** the copy's semaphores aren't shared with OpenGL.
   Start with a conservative wait (Remix's `Present` before the copy, and a
   finish on the copy), measure the cost, refine if needed.
2. **The import itself:** whether NVIDIA's OpenGL accepts the KMT handle, and
   whether the memory needs a dedicated allocation. Tested first, with a solid
   color copied from Remix.
3. **Resizing:** recreate the shared texture and Remix's device size when the
   resolution changes.
4. **Alt+X:** Remix hooks the window of its device; with a hidden window the
   hook sits on that window. Key forwarding, or our own toggle through
   `SetConfigVariable("rtx.showUI", ...)`.
5. **Frame rate** at 1440p, and the cost of rendering the scene in both APIs
   (kept until `cRendererRemix` builds its own list).

Fallback if the import fails: read Remix's frame back to the CPU and upload it
to OpenGL. That's slow, but enough to keep going.

### Build order
1. Shared texture + OpenGL import, proven with a solid color.
2. Remix's frame shown in place of the world, with the HUD on top, and F9 working
   in both directions.
3. Measure the frame rate.
4. `cRendererRemix`: Remix's own renderer class with its own scene traversal,
   so RTX mode skips the OpenGL world render. Then the content work: material
   conversion, particles, skinned characters, the veil.

## Known leads
- Windows x64: each level load logs "Could not open binary file ...map_cache_n236"
  (expected, no cache yet); harmless noise.
- Physics feel with Newton 2.36 vs 2.00 still to be judged in play.
- Windows x64 log: "No valid sound devices available! Using implementation
  default", then the default OpenAL Soft device opens fine. Settings store
  `Device="0"`; cause not checked yet.
- Windows: Steam's `DevIL.dll` is 1.6.8 (2006); the engine expects 1.7.8 and logs
  "Cube Maps may not load correctly". The official Steam build ships the same DLL.
- Log error at menu load: `Sampler aGoboMap does not exist, could not bind it to unit 5`.
- Log shows `ShaderModel 3: 0` / `ShaderModel 4: 0` on macOS's legacy GL 2.1
  context; unknown how much quality that disables.
- macOS launcher disabled (`Launcher_Prefix.pch` missing from the release).
- SDL 1.2 code paths (`#if SDL_VERSION_ATLEAST(2,0,0)` branches in ~20 files)
  are dead in every current build and could be removed.
