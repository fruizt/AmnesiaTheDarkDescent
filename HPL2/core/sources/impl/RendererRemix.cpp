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

#include "impl/RemixScene.h"	// first: it includes windows.h with NOMINMAX
#include "impl/RemixContext.h"

#include "impl/RendererRemix.h"

#include "graphics/Graphics.h"
#include "graphics/LowLevelGraphics.h"
#include "graphics/FrameBuffer.h"
#include "graphics/Texture.h"
#include "graphics/RenderList.h"
#include "graphics/Renderable.h"
#include "graphics/Material.h"
#include "graphics/MaterialType.h"
#include "graphics/VertexBuffer.h"
#include "math/Frustum.h"
#include "math/Math.h"
#include "scene/World.h"
#include "scene/RenderableContainer.h"
#include "system/LowLevelSystem.h"

#include <cstdlib>

namespace hpl {

	//////////////////////////////////////////////////////////////////////////
	// HELPERS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	static float EnvFloat(const char *asName, float afDefault)
	{
		const char *sValue = getenv(asName);
		return sValue ? (float)atof(sValue) : afDefault;
	}

	static bool EnvFlag(const char *asName)
	{
		const char *sValue = getenv(asName);
		return sValue && sValue[0] == '1';
	}

	static double NowMs()
	{
		static double fTimerToMs = 0.0;
		if(fTimerToMs == 0.0)
		{
			LARGE_INTEGER freq;
			QueryPerformanceFrequency(&freq);
			fTimerToMs = 1000.0 / (double)freq.QuadPart;
		}
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		return (double)now.QuadPart * fTimerToMs;
	}

	//-----------------------------------------------------------------------

	// Squared distance from a point to an axis-aligned box (0 inside it)
	static float SqrDistToBox(const cVector3f &avPoint, const cVector3f &avMin, const cVector3f &avMax)
	{
		float fSqrDist = 0;
		for(int i=0; i<3; ++i)
		{
			float fD = 0;
			if(avPoint.v[i] < avMin.v[i])		fD = avMin.v[i] - avPoint.v[i];
			else if(avPoint.v[i] > avMax.v[i])	fD = avPoint.v[i] - avMax.v[i];
			fSqrDist += fD*fD;
		}
		return fSqrDist;
	}

	//-----------------------------------------------------------------------

	// Steps of an RTX frame, for the timings
	enum eRemixStep
	{
		eRemixStep_Frame,	// start of one RTX frame to the start of the next
		eRemixStep_Gather,	// collecting objects and lights within the radius
		eRemixStep_Scene,	// camera, objects and lights handed to Remix
		eRemixStep_Present,	// Remix's Present call
		eRemixStep_CopyWait,// copy into the shared texture, and waiting for it
		eRemixStep_Draw,	// OpenGL drawing it
		eRemixStep_LastEnum
	};
	static const char *gvRemixStepNames[eRemixStep_LastEnum] = { "frame", "gather", "scene", "present", "copy+wait", "draw" };

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// CONSTRUCTORS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	cRendererRemix::cRendererRemix(cGraphics *apGraphics, cResources* apResources)
		: iRenderer("Remix", apGraphics, apResources, 0)
	{
		// The render target is bound at the start; Remix's frame covers it all
		mbSetFrameBufferAtBeginRendering = true;
		mbClearFrameBufferAtBeginRendering = false;

		mpContext = NULL;
		mpScene = NULL;
		mpOutputTexture = NULL;
		mpOutputBuffer = NULL;

		mbRtxActive = false;
		mbToggleKeyWasDown = false;
		mlLastClaimedFrame = -1;
		mfRadius = 50.0f;
		mbDiag = false;
		mfLastDiagLog = -1;

		for(int i=0; i<eRemixStep_LastEnum; ++i) mvTimingSum[i] = 0;
		mfTimingMaxFrame = 0;
		mlTimingFrames = 0;
		mfTimingWindowStart = -1;
		mfLastFrameStart = -1;
		mfGatherMs = 0;
	}

	//-----------------------------------------------------------------------

	cRendererRemix::~cRendererRemix()
	{
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PUBLIC METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	bool cRendererRemix::LoadData()
	{
		// Never fails: without Remix the game just stays in classic mode
		if(EnvFlag("HPL_REMIX")==false) return true;

		////////////////////////////////////
		// Remix starts once per process (it can't be started twice). Graphics
		// option changes call DestroyData and LoadData again, which only
		// recreates the output buffer. Its size is still the one from startup.
		cVector2l vSize = mpLowLevelGraphics->GetScreenSizeInt();
		if(mpContext==NULL)
		{
			mpContext = hplNew(cRemixContext, ());
			if(mpContext->Init(vSize.x, vSize.y)==false) Error("Remix: not available, RTX mode is off\n");
		}
		if(mpContext->IsReady()==false) return true;

		if(mpScene==NULL)
		{
			mfRadius = EnvFloat("HPL_REMIX_RADIUS", 50.0f);
			float fLightScale = EnvFloat("HPL_REMIX_LIGHT_SCALE", 100.0f);
			bool bFlipV = EnvFlag("HPL_REMIX_FLIP_V");
			bool bNormalMaps = EnvFlag("HPL_REMIX_NORMAL_MAPS");
			mpScene = hplNew(cRemixScene, (mpContext->GetApi(), fLightScale, bFlipV, bNormalMaps));
			mbRtxActive = EnvFlag("HPL_REMIX_START_RTX");
			mbDiag = EnvFlag("HPL_REMIX_DIAG");

			Log("Remix: renderer ready at %dx%d, %s mode, F9 toggles (radius %.1f m, light scale %.1f, flip V %d, normal maps %d)\n",
				vSize.x, vSize.y, mbRtxActive ? "RTX" : "classic", mfRadius, fLightScale, bFlipV, bNormalMaps);
		}

		////////////////////////////////////
		// Output buffer for the post effects
		mpOutputTexture = mpGraphics->CreateTexture("RemixOutput", eTextureType_Rect, eTextureUsage_RenderTarget);
		mpOutputTexture->CreateFromRawData(cVector3l(vSize.x, vSize.y, 0), ePixelFormat_RGBA, NULL);
		mpOutputTexture->SetWrapSTR(eTextureWrap_ClampToEdge);

		mpOutputBuffer = mpGraphics->CreateFrameBuffer("Remix_Output");
		mpOutputBuffer->SetTexture2D(0, mpOutputTexture);
		mpOutputBuffer->CompileAndValidate();
		return true;
	}

	//-----------------------------------------------------------------------

	void cRendererRemix::DestroyData()
	{
		if(mpOutputBuffer) mpGraphics->DestroyFrameBuffer(mpOutputBuffer);
		if(mpOutputTexture) mpGraphics->DestroyTexture(mpOutputTexture);
		mpOutputBuffer = NULL;
		mpOutputTexture = NULL;
		// Remix itself lives until the process ends (its runtime isn't reloaded)
	}

	//-----------------------------------------------------------------------

	iTexture* cRendererRemix::GetPostEffectTexture()
	{
		return mpOutputTexture;
	}

	//-----------------------------------------------------------------------

	void cRendererRemix::UpdateModeToggle()
	{
		if(mpScene==NULL) return;

		// Only while the game window has focus
		bool bKeyDown = mpLowLevelGraphics->GetWindowInputFocus() && (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
		if(bKeyDown && mbToggleKeyWasDown==false)
		{
			mbRtxActive = !mbRtxActive;
			Log("Remix: switched to %s mode\n", mbRtxActive ? "RTX" : "classic");

			// Don't mix the two modes' frames in one average
			mlTimingFrames = 0;
			mfTimingWindowStart = -1;
			mfLastFrameStart = -1;
		}
		mbToggleKeyWasDown = bKeyDown;
	}

	//-----------------------------------------------------------------------

	bool cRendererRemix::ClaimViewport()
	{
		if(mpScene==NULL || mbRtxActive==false) return false;

		int lFrame = GetRenderFrameCount();
		if(lFrame == mlLastClaimedFrame) return false;
		mlLastClaimedFrame = lFrame;
		return true;
	}

	//-----------------------------------------------------------------------

	void cRendererRemix::OnWorldDestroyed()
	{
		if(mpScene) mpScene->ClearCaches();
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PRIVATE METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	void cRendererRemix::CopyToFrameBuffer()
	{
		// Nothing: without post effects, RenderObjects draws to the render target
	}

	//-----------------------------------------------------------------------

	void cRendererRemix::SetupRenderList()
	{
		double fStart = NowMs();

		// The render list does the per-frame updates (skinning, materials,
		// camera-facing billboards) and sorts objects and lights
		mpCurrentRenderList->Setup(mfCurrentFrameTime, mpCurrentFrustum);

		tRenderableFlag lNeededFlags = mpCurrentSettings->mbIsReflection ? eRenderableFlag_VisibleInReflection : eRenderableFlag_VisibleInNonReflection;
		cVector3f vCenter = mpCurrentFrustum->GetOrigin();
		for(int i=0; i<eWorldContainerType_LastEnum; ++i)
		{
			iRenderableContainer *pContainer = mpCurrentWorld->GetRenderableContainer((eWorldContainerType)i);
			pContainer->UpdateBeforeRendering();
			AddNodeObjectsInRadius(pContainer->GetRoot(), vCenter, lNeededFlags);
		}

		mfGatherMs = NowMs() - fStart;
	}

	//-----------------------------------------------------------------------

	// Like the deferred renderer's brute force walk, with a sphere around the
	// camera instead of the view frustum
	void cRendererRemix::AddNodeObjectsInRadius(iRenderableContainerNode *apNode, const cVector3f &avCenter, tRenderableFlag alNeededFlags)
	{
		apNode->UpdateBeforeUse();

		float fSqrRadius = mfRadius * mfRadius;

		// Always walk the root node
		if(apNode->GetParent())
		{
			if(apNode->UsesFlagsAndVisibility() &&
				(apNode->HasVisibleObjects()==false || (apNode->GetRenderFlags() & alNeededFlags) != alNeededFlags))
			{
				return;
			}
			if(SqrDistToBox(avCenter, apNode->GetMin(), apNode->GetMax()) > fSqrRadius) return;
		}

		if(apNode->HasChildNodes())
		{
			tRenderableContainerNodeListIt childIt = apNode->GetChildNodeList()->begin();
			for(; childIt != apNode->GetChildNodeList()->end(); ++childIt)
				AddNodeObjectsInRadius(*childIt, avCenter, alNeededFlags);
		}

		if(apNode->HasObjects())
		{
			tRenderableListIt it = apNode->GetObjectList()->begin();
			for(; it != apNode->GetObjectList()->end(); ++it)
			{
				iRenderable *pObject = *it;
				if(CheckObjectIsVisible(pObject, alNeededFlags)==false) continue;

				cBoundingVolume *pBV = pObject->GetBoundingVolume();
				if(SqrDistToBox(avCenter, pBV->GetMin(), pBV->GetMax()) > fSqrRadius) continue;

				mpCurrentRenderList->AddObject(pObject);
			}
		}
	}

	//-----------------------------------------------------------------------

	void cRendererRemix::RenderObjects()
	{
		double vStepMs[eRemixStep_LastEnum] = {};
		vStepMs[eRemixStep_Gather] = mfGatherMs;
		double fStepStart = NowMs();

		////////////////////////////////
		// The scene, for this frame
		mpScene->BeginFrame();	// drops last frame's lights and dynamic meshes
		mpScene->SetCamera(mpCurrentFrustum);

		for(int i=0; i<mpCurrentRenderList->GetSolidObjectNum(); ++i)
			mpScene->DrawObject(mpCurrentRenderList->GetSolidObject(i), mpCurrentFrustum);
		for(int i=0; i<mpCurrentRenderList->GetTransObjectNum(); ++i)
			mpScene->DrawObject(mpCurrentRenderList->GetTransObject(i), mpCurrentFrustum);

		for(int i=0; i<mpCurrentRenderList->GetLightNum(); ++i)
			mpScene->DrawLight(mpCurrentRenderList->GetLight(i));

		double fNow = NowMs();
		vStepMs[eRemixStep_Scene] = fNow - fStepStart;
		fStepStart = fNow;

		////////////////////////////////
		// Render, then bring Remix's frame to OpenGL
		mpContext->Present();

		fNow = NowMs();
		vStepMs[eRemixStep_Present] = fNow - fStepStart;
		fStepStart = fNow;

		mpContext->CopyFrame();

		fNow = NowMs();
		vStepMs[eRemixStep_CopyWait] = fNow - fStepStart;
		fStepStart = fNow;

		// Into our buffer for the post effects, else straight to the render target
		if(mbSendFrameBufferToPostEffects)	SetFrameBuffer(mpOutputBuffer, false);
		else								SetFrameBuffer(mpCurrentRenderTarget->mpFrameBuffer, true);
		mpContext->DrawFrame();

		vStepMs[eRemixStep_Draw] = NowMs() - fStepStart;
		RecordTimings(vStepMs);

		if(mbDiag && NowMs() - mfLastDiagLog > 2000.0)
		{
			mfLastDiagLog = NowMs();
			LogObjectsNearCamera();
		}
	}

	//-----------------------------------------------------------------------

	// Diagnostic for things that follow the camera (the "veil", the hands):
	// what each object near the camera is and where it sits in view space
	// (x right, y up, -z forward)
	void cRendererRemix::LogObjectsNearCamera()
	{
		static const char *vTypeNames[eRenderableType_LastEnum] = {
			"particles", "submesh", "light", "billboard", "beam", "guiset", "rope", "fogarea", "dummy" };

		cVector3f vCamPos = mpCurrentFrustum->GetOrigin();
		const cMatrixf &mtxView = mpCurrentFrustum->GetViewMatrix();

		Log("Remix diag: objects within 1.5 m of the camera at (%.2f %.2f %.2f):\n", vCamPos.x, vCamPos.y, vCamPos.z);
		int lCount = 0;
		for(int lList=0; lList<2; ++lList)
		{
			int lNum = lList==0 ? mpCurrentRenderList->GetSolidObjectNum() : mpCurrentRenderList->GetTransObjectNum();
			for(int i=0; i<lNum; ++i)
			{
				iRenderable *pObject = lList==0 ? mpCurrentRenderList->GetSolidObject(i) : mpCurrentRenderList->GetTransObject(i);
				cBoundingVolume *pBV = pObject->GetBoundingVolume();
				if(SqrDistToBox(vCamPos, pBV->GetMin(), pBV->GetMax()) > 1.5f*1.5f) continue;

				cMaterial *pMaterial = pObject->GetMaterial();
				iMaterialType *pType = pMaterial ? pMaterial->GetType() : NULL;
				iVertexBuffer *pVtx = pObject->GetVertexBuffer();
				cVector3f vViewCenter = cMath::MatrixMul(mtxView, pBV->GetWorldCenter());
				cVector3f vSize = pBV->GetSize();
				int lTypeIdx = (int)pObject->GetRenderType();

				Log("  %s %s '%s' material '%s' (type %s, blend %d, alpha %d), %d vtx, view center (%.2f %.2f %.2f), size (%.2f %.2f %.2f)\n",
					lList==0 ? "solid" : "trans",
					lTypeIdx >= 0 && lTypeIdx < eRenderableType_LastEnum ? vTypeNames[lTypeIdx] : "?",
					pObject->GetName().c_str(),
					pMaterial ? pMaterial->GetName().c_str() : "-",
					pType ? pType->GetName().c_str() : "-",
					pMaterial ? (int)pMaterial->GetBlendMode() : -1,
					pMaterial ? (int)pMaterial->GetAlphaMode() : -1,
					pVtx ? pVtx->GetVertexNum() : 0,
					vViewCenter.x, vViewCenter.y, vViewCenter.z, vSize.x, vSize.y, vSize.z);
				++lCount;
			}
		}
		Log("  (%d objects)\n", lCount);
	}

	//-----------------------------------------------------------------------

	// One frame's step times; logs the averages every 5 seconds
	void cRendererRemix::RecordTimings(const double *apStepMs)
	{
		double fNow = NowMs();
		double fFrameMs = mfLastFrameStart >= 0 ? fNow - mfLastFrameStart : -1;
		mfLastFrameStart = fNow;
		if(fFrameMs < 0) return;	// no complete frame yet

		if(mfTimingWindowStart < 0 || mlTimingFrames == 0)
		{
			for(int i=0; i<eRemixStep_LastEnum; ++i) mvTimingSum[i] = 0;
			mfTimingMaxFrame = 0;
			mlTimingFrames = 0;
			mfTimingWindowStart = fNow;
		}

		mvTimingSum[eRemixStep_Frame] += fFrameMs;
		if(fFrameMs > mfTimingMaxFrame) mfTimingMaxFrame = fFrameMs;
		for(int i=eRemixStep_Frame+1; i<eRemixStep_LastEnum; ++i) mvTimingSum[i] += apStepMs[i];
		++mlTimingFrames;

		if(fNow - mfTimingWindowStart < 5000.0) return;

		// "Remix perf: 60.0 fps | frame 16.67 avg, 18.20 max | gather 0.40 | ..."
		char sLine[512];
		double fFrameAvg = mvTimingSum[eRemixStep_Frame] / mlTimingFrames;
		int lLen = sprintf_s(sLine, "Remix perf: %.1f fps | frame %.2f avg, %.2f max", 1000.0 / fFrameAvg, fFrameAvg, mfTimingMaxFrame);
		for(int i=eRemixStep_Frame+1; i<eRemixStep_LastEnum && lLen > 0; ++i)
			lLen += sprintf_s(sLine + lLen, sizeof(sLine) - lLen, " | %s %.2f", gvRemixStepNames[i], mvTimingSum[i] / mlTimingFrames);
		Log("%s (ms, %d frames; %d objects, %d lights, %d meshes / %d materials cached, stale rebuilt: %d / %d)\n",
			sLine, mlTimingFrames,
			mpCurrentRenderList->GetSolidObjectNum() + mpCurrentRenderList->GetTransObjectNum(), mpCurrentRenderList->GetLightNum(),
			mpScene->GetMeshNum(), mpScene->GetMaterialNum(), mpScene->GetStaleMeshHits(), mpScene->GetStaleMaterialHits());

		mlTimingFrames = 0;
	}

	//-----------------------------------------------------------------------
}
