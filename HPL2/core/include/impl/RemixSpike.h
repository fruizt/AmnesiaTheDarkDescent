/*
 * Copyright © 2009-2020 Frictional Games
 *
 * This file is part of Amnesia: The Dark Descent.
 *
 * Amnesia: The Dark Descent is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * Amnesia: The Dark Descent is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with Amnesia: The Dark Descent.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef HPL_REMIX_SPIKE_H
#define HPL_REMIX_SPIKE_H

namespace hpl {

	class iRenderer;
	class cCamera;

	//-----------------------------------------

	// Phase 1 of the RTX Remix spike (see docs/MODERNIZATION.md).
	// After the OpenGL renderer draws the world, mirror what it drew (visible
	// objects, their materials, the lights and the camera) into RTX Remix, which
	// presents path-traced frames into the game window. F9 toggles between classic
	// (OpenGL) and RTX (Remix) mode; Alt+X opens Remix's menu in RTX mode. The
	// game's own menus/HUD (OpenGL) don't show in RTX mode yet. Only active when the
	// environment variable HPL_REMIX=1 is set and the build has HPL_REMIX (64-bit Windows).
	//
	// Options (environment variables):
	//   HPL_REMIX_START_RTX    1 = start in RTX mode (default: classic)
	//   HPL_REMIX_LIGHT_SCALE  multiplier from Amnesia light color to Remix radiance (default 100)
	//   HPL_REMIX_FLIP_V       1 = flip texture V coordinates (default 0)
	//   HPL_REMIX_NORMAL_MAPS  1 = pass normal maps to Remix (default 0)
	class cRemixSpike
	{
	public:
		static void OnWorldRendered(iRenderer *apRenderer, cCamera *apCamera);

		// Called before the engine destroys a world: everything cached from it
		// (keyed by engine pointers, which a new world may reuse) becomes invalid
		static void OnWorldDestroyed();

		// True in RTX mode: Remix presents the frame, so the engine must skip
		// OpenGL's SwapBuffers (both present to the same window)
		static bool IsRtxActive();
	};

	//-----------------------------------------

};
#endif // HPL_REMIX_SPIKE_H
