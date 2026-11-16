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

#ifndef HPL_RENDERER_REMIX_H
#define HPL_RENDERER_REMIX_H

#include "graphics/Renderer.h"

namespace hpl {

	//---------------------------------------------

	class cRemixContext;
	class cRemixScene;
	class iRenderableContainerNode;
	class iLowLevelGraphics;

	//---------------------------------------------

	// RTX mode's renderer (docs/decisions/0002): path traced by RTX Remix. It
	// sends Remix everything within a radius of the camera, in all directions
	// (what's behind the player still casts shadows, bounces light and shows in
	// reflections), and draws Remix's frame where the world goes. Post effects
	// and the GUI then draw on top as with the deferred renderer.
	//
	// It sits beside the deferred renderer (eRenderer_Main), which the game
	// relies on. In RTX mode cScene uses this one for the first world viewport
	// of each frame. F9 toggles the mode.
	//
	// Only active when the environment variable HPL_REMIX=1 is set.
	// Options (environment variables):
	//   HPL_REMIX_START_RTX    1 = start in RTX mode (default: classic)
	//   HPL_REMIX_RADIUS       meters around the camera sent to Remix (default 50)
	//   HPL_REMIX_LIGHT_SCALE  multiplier from Amnesia light color to Remix radiance (default 100)
	//   HPL_REMIX_FLIP_V       1 = flip texture V coordinates (default 0)
	//   HPL_REMIX_NORMAL_MAPS  1 = pass normal maps to Remix (default 0)
	// Every 5 seconds in RTX mode it logs frame timings ("Remix perf" in hpl.log).
	class cRendererRemix : public iRenderer
	{
	public:
		cRendererRemix(cGraphics *apGraphics, cResources* apResources);
		~cRendererRemix();

		bool LoadData();
		void DestroyData();

		iTexture* GetPostEffectTexture();

		// Once per frame, before rendering: F9 switches between classic and RTX
		void UpdateModeToggle();
		bool IsRtxActive(){ return mbRtxActive; }

		// True if this renderer should draw the viewport about to be rendered:
		// RTX mode, Remix ready, and no other viewport drawn by it this frame
		// (Remix takes one scene per frame)
		bool ClaimViewport();

		// Before the engine destroys a world: drop what was cached from it
		void OnWorldDestroyed();

	private:
		void CopyToFrameBuffer();
		void SetupRenderList();
		void RenderObjects();

		void AddNodeObjectsInRadius(iRenderableContainerNode *apNode, const cVector3f &avCenter, tRenderableFlag alNeededFlags);
		void RecordTimings(const double *apStepMs);

		cRemixContext *mpContext;
		cRemixScene *mpScene;

		// Remix's frame for the post effects (same kind of texture as the
		// deferred renderer's accumulation buffer)
		iTexture *mpOutputTexture;
		iFrameBuffer *mpOutputBuffer;

		bool mbRtxActive;
		bool mbToggleKeyWasDown;
		int mlLastClaimedFrame;
		float mfRadius;

		// Timings, averaged over 5 seconds
		double mvTimingSum[6];
		double mfTimingMaxFrame;
		int mlTimingFrames;
		double mfTimingWindowStart;
		double mfLastFrameStart;
		double mfGatherMs;
	};

	//---------------------------------------------

};
#endif // HPL_RENDERER_REMIX_H
