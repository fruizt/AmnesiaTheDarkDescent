# 0003: RTX mode's look: light from real sources, not classic mode's tricks

- **Status:** Accepted
- **Date:** 2026-11-16
- **Builds on:** [0001](0001-remix-frame-shown-through-opengl.md),
  [0002](0002-renderer-remix.md)

## Context
With `cRendererRemix` working, we tried to make RTX mode look like classic
mode by translating each of its lighting features into Remix. It didn't
converge. Amnesia's look is built from tricks hand-tuned per room for a
rasterizer:
- **box lights** fake bounce light;
- **spot lights with gobo textures** fake moonlight through windows and planks;
- **light-ray billboards and area fog cards** fake light shafts and mist;
- the lantern casts no shadows and never over-brightens;
- custom falloff curves.

A path tracer can't use most of these literally. Each translation was a guess
about the artist's intent, and changed little (fog from the level data) or
traded one problem for another (the lantern, weakened to stop blinding the
hand, stopped lighting the room). Screenshot-and-tweak rounds didn't settle
anything.

## Options considered
1. **Faithful:** recreate classic mode's look, per room. Rejected: it means
   rebuilding every trick, room by room, with a renderer that works
   differently, and it wastes what path tracing can do.
2. **Reimagined:** keep the mood (darkness, a warm lantern, cold moonlight),
   but light comes from real sources: lamps, flames, the lantern, the moon and
   sky through real openings, glowing materials. Path tracing does the bounce
   light, shadows, shafts and reflections. Chosen.

## Decision
**RTX mode is lit by real light sources, and the classic tricks stay in
classic mode.** Box lights, gobos, light-ray billboards and area fog cards are
not translated. What they faked comes from the real thing: bounce light from
path tracing, moonlight from a real moon and sky through the real openings,
shafts and mist from volumetric air.

Two tracks:
- **Engine (code):** give Remix correct, stable data. That means meshes,
  materials and lights with stable identities, real light sources, physically
  sensible materials, and the atmosphere.
- **Art direction (by eye, with Remix's tools):** the look is tuned live with
  the Alt+X menu, and per level where needed, not by guessing values in code.

## Plan
**A. Foundation**
1. **Stable hashes.** Meshes, materials, textures and lights are keyed by
   memory addresses that change every run (also the cause of the stale-cache
   bugs). Hash them from what they are instead: asset and material file
   paths, submesh index, and for lights the map plus the light's name. Remix's
   replacement workflow (the RTX Remix Toolkit, mods) keys everything by these
   hashes, so per-object art work can only be saved once they are stable.
   Research alongside: whether the Toolkit can capture a scene from an API
   app.

**B. Surfaces that don't look like plastic**
2. **Amnesia's own maps.** Today every surface gets one constant roughness and
   no normal maps, so stone, wood, metal and skin all look like smooth
   plastic. Amnesia ships normal, specular and often height maps for almost
   every material: pass the normal maps (with the right convention), convert
   specular to roughness, use height where Remix can.
3. **AI-enhanced textures.** The RTX Remix Toolkit's AI texture tools turn an
   old texture into an upscaled albedo plus generated normal, roughness and
   height maps. Engine side: a replacement folder checked before the original
   textures (`<name>_albedo`, `_normal`, `_roughness`, `_height`), so RTX mode
   uses the full set, and classic mode the albedo and normal. Also a list of
   the textures each level uses, ordered by how much of the screen they cover,
   so the most visible get done first, and research into the Toolkit's batch
   processing. The Toolkit is run by hand (it's a desktop app). Derived
   textures can't be shared publicly without Frictional's permission.

**C. Real light sources**
4. **The lantern at full strength, the hands as a view model.** The lantern
   lights the room again. The hand problem is solved through Remix's
   `VIEW_MODEL` instance category, not by weakening the light.
5. **Glowing materials as light:** flames, candles, lantern glass and torches
   emit through their illumination maps (calibrated emissive intensity), so
   Remix's emissive lighting picks them up.
6. **Moon and sky:** a distant light (the moon) and sky light that enter only
   through real openings. Direction, color and strength per level, from a
   small data file we own (not the game's).
7. **Light sizes:** emitters sized like their real source (candle flame,
   torch, lamp), for natural soft shadows.

**D. The rest of the materials, and the atmosphere**
8. **Materials:** translucency and water, the trail and decals.
9. **Atmosphere:** volumetric air from the level's fog settings (done), tuned
   by eye.

**E. Art pass**
10. Rainy Hall and Old Archives first, tuned with Alt+X and, once hashes are
    stable, the Toolkit. Settings that work become defaults.

**F. Remaining engine work (as needed)**
Skinned meshes through Remix's skinning if enemies are expensive, the game's
own effects (outlines, flashes) through Remix's depth output, resolution
changes, an option in the game's menu instead of F9.

## Consequences
- RTX mode will look different from classic mode, by intention. Classic mode
  stays the faithful original.
- Success is judged by the mood, not by matching classic screenshots.
- The light calibration against classic falloff (0.1875 × color × R²) stays
  as a starting brightness for the level's lamps. Art direction adjusts it.
- Stable hashes come first: they're invisible in screenshots, but every saved
  art change depends on them.
