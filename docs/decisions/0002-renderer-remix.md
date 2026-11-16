# 0002: cRendererRemix, a renderer of its own for RTX mode

- **Status:** Accepted
- **Date:** 2026-11-16
- **Builds on:** [0001](0001-remix-frame-shown-through-opengl.md) (Remix renders
  off-screen, OpenGL shows its frame)

## Context
RTX mode works through a **spike**: `cRemixSpike`, hooked into `cScene::Render`
after the OpenGL renderer has drawn the world. The spike copies the OpenGL
renderer's list of visible objects and lights into Remix. That proved the
approach (path tracing, 60 FPS at 1440p, HUD on top), but it can't be the
real thing:

- **OpenGL renders the whole world for nothing** in RTX mode (~1–2 ms of CPU, plus
  its GPU time), only so the spike can read its render list.
- **The wrong objects reach Remix.** The deferred renderer keeps what the camera
  sees (frustum and GPU occlusion queries). A path tracer also needs what the
  camera *doesn't* see: objects behind the player cast shadows, bounce light
  and appear in reflections, and lights out of view still light the room.
- **Animated characters freeze.** The engine skins them on the CPU into a
  dynamic vertex buffer every frame (`cSubMeshEntity::UpdateGraphicsForFrame`).
  The spike caches each mesh's first shape.
- It's a hook in `cScene`, not part of the engine's renderer design.

How HPL2 renderers work (`graphics/Renderer.h`):
- `iRenderer::Render` is a template method: `BeginRendering`, then the
  virtuals `SetupRenderList` and `RenderObjects`, then `EndRendering`.
  `cRendererDeferred`, `cRendererSimple` and `cRendererWireFrame` implement it,
  and `cGraphics` creates one of each into `mvRenderers[eRenderer_*]`.
- Each `cViewport` has its renderer (`SetRenderer`). New viewports get
  `eRenderer_Main` (the deferred renderer).
- The game assumes `eRenderer_Main` **is** `cRendererDeferred`: `cLuxEffectRenderer`
  `static_cast`s it to reach its accumulation and depth buffers, and the water and
  translucent materials ask it for refraction and reflection textures.
- `cLuxEffectRenderer` draws the **interaction flash, object outlines and enemy
  glow** through renderer callbacks (`RenderSolid`, `RenderTrans`) into the
  deferred renderer's buffers, using its depth buffer.
- The world exposes every light (`cWorld::GetLightList`), the fog areas, the sky
  box, and its renderables in two containers (static, dynamic) with a bounding
  volume tree.

## Options considered
1. **Keep growing the spike.** Rejected: the double rendering, the wrong set of
   objects and the hook all stay.
2. **Replace `cRendererDeferred` as `eRenderer_Main` in RTX mode.** Rejected: the
   game's `static_cast` and the material lookups would break, and classic mode
   must stay available at the press of F9.
3. **Make the deferred renderer able to skip drawing** and still produce its
   list for Remix. Rejected: its visibility comes from drawing (occlusion
   queries need depth drawn first), and it's the wrong set of objects anyway.
4. **A new renderer, `cRendererRemix`, next to the others.** Chosen.

## Decision
Add **`cRendererRemix : iRenderer`** as **`eRenderer_Remix`**, created by
`cGraphics` next to the others (64-bit Windows builds with `HPL_REMIX`). RTX
mode is the game's main viewport using it, so F9 (later a menu setting) calls
`SetRenderer` on the viewport. The deferred renderer is untouched and remains
`eRenderer_Main`.

Split in three parts:
- **`cRemixContext`** (`impl/`): owns Remix. It loads the DLL, creates the
  device, the hidden window, the shared texture and its OpenGL import, handles
  resizing, and does the per-frame "present, copy, wait". That's the code that
  is in the spike today, moved as is. One instance, created at startup when
  RTX is available.
- **`cRendererRemix`** (`impl/`, Windows only): an `iRenderer`.
  - `SetupRenderList`: gathers what Remix needs, **by distance, not by view**.
    That's every static and dynamic renderable whose bounds are within a
    radius of the camera (a tree query on the world's containers), and every
    active light from `GetLightList` within that radius. The radius starts
    around the world's fog end or 50 m, and we tune it. Billboards, particles,
    beams and ropes are included as their types get supported.
  - `RenderObjects`: hands camera, instances and lights to Remix, then Remix's
    frame goes into the render target through `cRemixContext`, as the spike
    does now. Post effects and GUI then run on top unchanged.
- **Scene translation** (`impl/`, `cRemixScene` or similar): engine objects to
  Remix resources. Meshes, materials and lights, with their caches. It keeps
  the spike's lessons: caches cleared when a world is destroyed, entries
  checked against what they were made from, light-ray billboards skipped. It's
  separate from the renderer because it will grow most (material conversion,
  skinning, particles).

Skinned meshes use **Remix's skinning**: bone weights and indices on the mesh
once, bone transforms per instance each frame (`InstanceInfoBoneTransformsEXT`).
That avoids re-uploading every animated mesh every frame.

The spike is deleted once `cRendererRemix` renders what the spike renders.

## Consequences
- RTX mode no longer renders the world in OpenGL: the GPU and CPU time that
  costs comes back.
- Off-screen objects and lights contribute, so shadows, bounce light and
  reflections become correct.
- Classic mode and everything that depends on the deferred renderer stay as
  they are.
- **Open, and missing in RTX mode until solved:**
  - **The game's own effects** (interaction flash, outlines, enemy glow) need the
    renderer callbacks and a depth buffer. Remix can copy out its depth
    (`dxvk_CopyRenderingOutput(DEPTH)`); turning that into a depth buffer for
    the callbacks is its own step.
  - **Water reflection and refraction materials** ask `eRenderer_Main` for
    textures. In RTX mode Remix does reflection and refraction itself, so those
    materials need converting, not the textures.
  - **Fog areas and world fog** map to Remix's volumetrics, to be designed.
  - **The radius** trades completeness against per-frame cost (one `DrawInstance`
    per object per frame). Measured with the existing "Remix perf" timings.
  - **Reflection viewports** (`mbIsReflection`) and other viewports (menu,
    inventory, journal) stay on the deferred renderer for now.

## Build order
1. Move device and interop from the spike into `cRemixContext`. No behavior
   change.
2. `cRendererRemix` with the spike's mesh, material and light code moved into
   the scene translation, gathering by radius. F9 switches the main viewport's
   renderer. Delete the spike. Check against the spike: same image or better,
   and frame timings.
3. Skinned meshes through Remix's skinning (first animated enemy).
4. Billboards, particles, beams, ropes.
5. Depth output and the game's effect callbacks.
6. Materials (specular to roughness, translucency, water), fog.

## Evidence
- Spike timings: commit `6b212a2`. GL world ~1–2 ms CPU in RTX mode, which is
  wasted.
- Renderer structure: `graphics/Renderer.h` (`iRenderer::Render`),
  `sources/graphics/Graphics.cpp` (renderer creation),
  `sources/scene/Scene.cpp` (`CreateViewport` gives `eRenderer_Main`).
- The game's dependence on the deferred renderer:
  `amnesia/src/game/LuxEffectRenderer.cpp` (constructor `static_cast`),
  `MaterialType_Water.cpp` and `MaterialType_BasicTranslucent.cpp`
  (`GetRenderer(eRenderer_Main)`).
- CPU skinning: `sources/scene/SubMeshEntity.cpp`, `UpdateGraphicsForFrame`.
