# 0001: RTX mode: Remix renders off-screen, OpenGL shows its frame

- **Status:** Accepted
- **Date:** 2026-11-16
- **Branch:** `rtx-remix`

## Context
RTX mode uses the RTX Remix API: the engine hands Remix its meshes, materials,
lights and camera, and Remix path-traces them (decision of 2026-11-15 in
[MODERNIZATION.md](../MODERNIZATION.md)). Remix renders with Vulkan internally.
The game renders with OpenGL, and everything besides the 3D world is OpenGL too:
main menu, HUD, inventory, notes, and post effects such as bloom and the sanity
distortions.

So the question was **how Remix's image reaches the screen** together with all
of that. It decides how the whole RTX mode is built, and it was the main doubt
left after the spike showed that Remix can render Amnesia's levels.

What the spike found:
- A **second window** for Remix worked as a first test, but isn't a game:
  two windows, and Remix's own menu can't be used.
- **Taking turns on the game window** (Remix presents in RTX mode, OpenGL in
  classic mode, F9 switching) failed one way. Once Remix's Vulkan swapchain is
  attached to the window, OpenGL's `SwapBuffers` no longer reaches the screen:
  after switching back to classic, the game kept running but the window showed
  Remix's last frame. The game's menus and HUD were never visible in RTX mode
  either.

## Options considered
1. **Remix presents, OpenGL presents, taking turns on one window.** Rejected:
   Windows doesn't let the two APIs take turns on one window (see above).
2. **Restart Remix on every switch** to free the window. Rejected: seconds per
   switch, loses all of Remix's caches, and still no HUD in RTX mode.
3. **A child window** inside the game window for Remix, shown or hidden by F9.
   Rejected as a workaround: it fixes switching but not the HUD, menus and post
   effects in RTX mode, which would still need the real solution later.
4. **Remix's external swapchain** (`forceNoVkSwapchain` +
   `dxvk_GetExternalSwapchain`), which the API offers for "OpenGL interop".
   Rejected after reading dxvk-remix's source (`rtx_remix_api.cpp`,
   `d3d9_swapchain.h`): it returns raw `VkImage`/`VkSemaphore` handles of
   Remix's own Vulkan device, with no memory that can be shared, so OpenGL can't
   read them.
5. **A texture shared between Remix and OpenGL.** Chosen (below).
6. **Porting the engine to Vulkan** to render into Remix's frame ourselves.
   Not needed: option 5 works without it.

## Decision
**OpenGL is the only API that presents to the game window, in every mode.
Remix renders off-screen, and OpenGL draws Remix's image in place of the 3D
world.** Everything drawn after the world (post effects, 3D GUI, screen GUI)
then draws on top unchanged.

How it works:
- The engine creates Remix's D3D9 device itself instead of calling
  `remixapi_Startup`, doing the same steps (`dxvk_CreateD3D9`,
  `CreateDeviceEx`, `dxvk_RegisterD3D9Device`) but keeping the device. Remix's
  own swapchain goes to a **hidden window** of the game window's size.
- On that device it creates a **shared render target** (`D3DPOOL_DEFAULT`,
  `D3DUSAGE_RENDERTARGET`, a `pSharedHandle`). Remix backs it with Vulkan memory
  exported as an `OPAQUE_WIN32_KMT` handle.
- OpenGL **imports** that memory as a texture (`GL_EXT_memory_object` +
  `GL_EXT_memory_object_win32`: `glImportMemoryWin32HandleEXT`, then
  `glTexStorageMem2DEXT`). No copy: both sides see the same memory. Remix
  doesn't report the allocation's size, which the import needs, so it is
  searched in 64 KB steps from the image's size. The driver answers
  `GL_INVALID_VALUE` when the size is too small and `GL_OUT_OF_MEMORY` when it's
  too large.
- Each frame in RTX mode: Remix's `Present`, then
  `dxvk_CopyRenderingOutput(FINAL_COLOR)` into the shared texture, a D3D9 event
  query to wait for the copy, then OpenGL draws the texture over the world's
  render target.

## Consequences
- **The game's whole presentation layer works in RTX mode as is**: menus, HUD,
  inventory, interaction icons, and the screen effects (the player's shaky
  vision at the start of the game already applies to the RTX image).
- **F9 switches instantly both ways**, since OpenGL never gives up the window.
- **No Vulkan or DirectX port of the engine is needed.** Remix's Vulkan stays
  inside Remix.
- RTX mode becomes "Remix renders the world, the classic engine does the rest".
  The next renderer work (`cRendererRemix`) only has to produce the world.
- Costs and open points:
  - **Alt+X** (Remix's developer menu) belongs to the hidden window now and
    doesn't open. To solve: forward keys, or toggle `rtx.showUI` ourselves.
  - **Sync is conservative**: the CPU waits for Remix's copy every frame. No
    slowdown was noticed, but it hasn't been measured.
  - **Resizing** isn't handled yet: the shared texture and Remix's output keep
    the startup size.
  - The import needs `GL_EXT_memory_object_win32` and the KMT handle type.
    Confirmed on NVIDIA. Unknown on AMD/Intel, where Remix itself is
    unofficial. Classic mode never uses it, and the fallback is a CPU copy.
  - The size search relies on driver error codes, not a documented size.

## Evidence
- Failure of option 1: commit `2e5eb11` (known issues in its message).
- Option 4 ruled out, option 5 found: design in commit `2330c92`
  (MODERNIZATION.md, "RTX mode design").
- Implementation: commit `579c275`. Game log:
  `Remix: shared texture imported into OpenGL (1600x900, 6553600 bytes)`.
- Play test on the RTX 4070 Ti Super, Rainy Hall: path-traced image in the game
  window, HUD and inventory on top, F9 both ways, no noticeable slowdown.
