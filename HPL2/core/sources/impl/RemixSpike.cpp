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

// remix_c.h includes windows.h; keep its min/max macros away from the engine
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <remix/remix_c.h>

#include "impl/RemixSpike.h"

#include "graphics/Renderer.h"
#include "graphics/RenderList.h"
#include "graphics/Renderable.h"
#include "graphics/VertexBuffer.h"
#include "graphics/Material.h"
#include "graphics/MaterialType.h"
#include "graphics/MaterialType_BasicTranslucent.h"
#include "graphics/Texture.h"
#include "scene/Camera.h"
#include "scene/Light.h"
#include "scene/LightSpot.h"
#include "system/LowLevelSystem.h"
#include "math/Math.h"

#include <cstdlib>
#include <map>
#include <utility>
#include <vector>

namespace hpl {

	//////////////////////////////////////////////////////////////////////////
	// STATE
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	static remixapi_Interface gRemix = {};
	static HMODULE ghRemixDll = NULL;
	static HWND ghRemixWindow = NULL;

	static bool gbStartupTried = false;
	static bool gbRunning = false;
	static int glLastFrame = -1;

	static float gfLightScale = 100.0f;
	static bool gbFlipV = false;
	static bool gbNormalMaps = false;

	// Remix resources, created once and reused
	typedef std::pair<iVertexBuffer*, cMaterial*> tMeshKey;
	static std::map<cMaterial*, remixapi_MaterialHandle> gmapMaterials;
	static std::map<tMeshKey, remixapi_MeshHandle> gmapMeshes;
	// Lights are recreated every frame (as the Remix docs say to do for changing lights)
	static std::vector<remixapi_LightHandle> gvFrameLights;

	//-----------------------------------------------------------------------

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

	//-----------------------------------------------------------------------

	static LRESULT CALLBACK RemixWindowProc(HWND ahWnd, UINT alMsg, WPARAM alWParam, LPARAM alLParam)
	{
		// Closing would destroy the window Remix presents to; minimize instead
		if(alMsg == WM_CLOSE)
		{
			ShowWindow(ahWnd, SW_MINIMIZE);
			return 0;
		}
		// The window only mirrors the game: clicking it must not take keyboard
		// focus away from the game window (Amnesia stops reading input then)
		if(alMsg == WM_MOUSEACTIVATE)
			return MA_NOACTIVATE;
		return DefWindowProcW(ahWnd, alMsg, alWParam, alLParam);
	}

	//-----------------------------------------------------------------------

	static bool Startup()
	{
		gbStartupTried = true;
		if(EnvFlag("HPL_REMIX")==false) return false;

		gfLightScale = EnvFloat("HPL_REMIX_LIGHT_SCALE", 100.0f);
		gbFlipV = EnvFlag("HPL_REMIX_FLIP_V");
		gbNormalMaps = EnvFlag("HPL_REMIX_NORMAL_MAPS");

		////////////////////////////////
		// Second window for Remix to present to.
		// SDL's event loop pumps all of this thread's messages, so it stays responsive.
		WNDCLASSW windowClass = {};
		windowClass.lpfnWndProc = RemixWindowProc;
		windowClass.hInstance = GetModuleHandleW(NULL);
		windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
		windowClass.lpszClassName = L"HplRemixSpike";
		RegisterClassW(&windowClass);

		RECT clientRect = { 0, 0, 1280, 720 };
		AdjustWindowRect(&clientRect, WS_OVERLAPPEDWINDOW, FALSE);
		// WS_EX_NOACTIVATE: never takes keyboard focus. WS_EX_APPWINDOW: still on the taskbar.
		ghRemixWindow = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_APPWINDOW, L"HplRemixSpike", L"Amnesia - RTX Remix (spike)",
									  WS_OVERLAPPEDWINDOW,
									  40, 40, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top,
									  NULL, NULL, windowClass.hInstance, NULL);
		ShowWindow(ghRemixWindow, SW_SHOWNOACTIVATE);

		////////////////////////////////
		// Load Remix from the exe's folder (it finds its dependencies there)
		wchar_t sDllPath[MAX_PATH];
		GetModuleFileNameW(NULL, sDllPath, MAX_PATH);
		wchar_t *pFileName = wcsrchr(sDllPath, L'\\');
		if(pFileName) wcscpy_s(pFileName + 1, MAX_PATH - (pFileName + 1 - sDllPath), L"d3d9.dll");

		remixapi_ErrorCode lResult = remixapi_lib_loadRemixDllAndInitialize(sDllPath, &gRemix, &ghRemixDll);
		if(lResult != REMIXAPI_ERROR_CODE_SUCCESS)
		{
			Error("Remix: could not load '%ls' (error %d)\n", sDllPath, lResult);
			return false;
		}

		remixapi_StartupInfo startupInfo = {};
		startupInfo.sType = REMIXAPI_STRUCT_TYPE_STARTUP_INFO;
		startupInfo.hwnd = ghRemixWindow;
		lResult = gRemix.Startup(&startupInfo);
		if(lResult != REMIXAPI_ERROR_CODE_SUCCESS)
		{
			Error("Remix: Startup failed (error %d)\n", lResult);
			return false;
		}

		// Remix's own UI (first-use guide, Alt+X menu) blocks all input to the
		// process while open, which froze the game's controls. Start with it
		// closed, and never block the game: the Remix window only mirrors it.
		gRemix.SetConfigVariable("rtx.showUI", "0");
		gRemix.SetConfigVariable("rtx.blockInputToGameInUI", "False");

		Log("Remix: started (light scale %.1f, flip V %d, normal maps %d)\n", gfLightScale, gbFlipV, gbNormalMaps);
		return true;
	}

	//-----------------------------------------------------------------------

	static remixapi_MaterialHandle GetRemixMaterial(cMaterial *apMaterial)
	{
		if(apMaterial==NULL) return NULL;

		std::map<cMaterial*, remixapi_MaterialHandle>::iterator it = gmapMaterials.find(apMaterial);
		if(it != gmapMaterials.end()) return it->second;

		// Amnesia's texture files, passed by path
		tWString sAlbedo, sNormal, sEmissive;
		iTexture *pTexture = apMaterial->GetTexture(eMaterialTexture_Diffuse);
		if(pTexture) sAlbedo = pTexture->GetFullPath();
		pTexture = apMaterial->GetTexture(eMaterialTexture_NMap);
		if(pTexture && gbNormalMaps) sNormal = pTexture->GetFullPath();
		pTexture = apMaterial->GetTexture(eMaterialTexture_Illumination);
		if(pTexture) sEmissive = pTexture->GetFullPath();

		// Defaults from the Remix C++ wrapper (remix.h), opaque, no alpha test
		remixapi_MaterialInfoOpaqueEXT opaqueInfo = {};
		opaqueInfo.sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT;
		opaqueInfo.albedoConstant = { 0.8f, 0.8f, 0.8f };
		opaqueInfo.opacityConstant = 1.0f;
		opaqueInfo.roughnessConstant = 0.7f;
		opaqueInfo.metallicConstant = 0.0f;
		opaqueInfo.thinFilmThickness_value = 200.0f;
		opaqueInfo.useDrawCallAlphaState = false;
		opaqueInfo.alphaTestType = 7;	// always pass
		opaqueInfo.enableDlssControlMask = true;
		opaqueInfo.dlssControlMaskIntensity = 1.0f;
		opaqueInfo.dlssControlMaskToneStrength = 0.3f;
		opaqueInfo.dlssControlMaskStructuralStrength = 0.7f;

		remixapi_MaterialInfo materialInfo = {};
		materialInfo.sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO;
		materialInfo.pNext = &opaqueInfo;
		materialInfo.hash = (uint64_t)(uintptr_t)apMaterial;
		materialInfo.albedoTexture = sAlbedo.empty() ? NULL : sAlbedo.c_str();
		materialInfo.normalTexture = sNormal.empty() ? NULL : sNormal.c_str();
		materialInfo.emissiveTexture = sEmissive.empty() ? NULL : sEmissive.c_str();
		materialInfo.emissiveIntensity = sEmissive.empty() ? 0.0f : 1.0f;
		materialInfo.spriteSheetRow = 1;
		materialInfo.spriteSheetCol = 1;
		materialInfo.filterMode = 1;	// linear
		materialInfo.wrapModeU = 1;		// repeat
		materialInfo.wrapModeV = 1;

		////////////////////////////////
		// Transparency, from the albedo texture's alpha channel.
		// Remix enums (surface_shared.h): AlphaTestType kGreater = 4;
		// BlendType kAlpha = 0, kAlphaEmissive = 1, kEmissive = 6,
		// kMultiplicative = 7, kDoubleMultiplicative = 8.
		if(apMaterial->GetAlphaMode() == eMaterialAlphaMode_Trans)
		{
			// Cut-outs (alpha test)
			opaqueInfo.alphaTestType = 4;
			opaqueInfo.alphaReferenceValue = 127;
		}

		iMaterialType *pType = apMaterial->GetType();
		if(pType && pType->IsTranslucent())
		{
			// HPL2 draws translucent materials unlit unless AffectedByLightLevel is
			// set (e.g. the glowing blood trail), so those glow in Remix too
			cMaterialType_Translucent_Vars *pVars = dynamic_cast<cMaterialType_Translucent_Vars*>(apMaterial->GetVars());
			bool bGlows = pVars && pVars->mbAffectedByLightLevel==false;

			int lBlendType = -1;
			switch(apMaterial->GetBlendMode())
			{
			case eMaterialBlendMode_Alpha:
			case eMaterialBlendMode_PremulAlpha:	lBlendType = bGlows ? 1 : 0; break;
			case eMaterialBlendMode_Add:			lBlendType = 6; break;
			case eMaterialBlendMode_Mul:			lBlendType = 7; break;
			case eMaterialBlendMode_MulX2:			lBlendType = 8; break;
			default: break;
			}
			if(lBlendType >= 0)
			{
				opaqueInfo.blendType_hasvalue = true;
				opaqueInfo.blendType_value = lBlendType;
			}
		}

		remixapi_MaterialHandle hMaterial = NULL;
		remixapi_ErrorCode lResult = gRemix.CreateMaterial(&materialInfo, &hMaterial);
		if(lResult != REMIXAPI_ERROR_CODE_SUCCESS)
			Warning("Remix: CreateMaterial failed for '%s' (error %d)\n", apMaterial->GetName().c_str(), lResult);

		gmapMaterials[apMaterial] = hMaterial;
		return hMaterial;
	}

	//-----------------------------------------------------------------------

	static remixapi_MeshHandle GetRemixMesh(iVertexBuffer *apVtxBuffer, cMaterial *apMaterial)
	{
		tMeshKey key(apVtxBuffer, apMaterial);
		std::map<tMeshKey, remixapi_MeshHandle>::iterator it = gmapMeshes.find(key);
		if(it != gmapMeshes.end()) return it->second;

		remixapi_MeshHandle hMesh = NULL;
		gmapMeshes[key] = NULL;	// also remembers failures, so they're tried once

		// The vertex buffer's CPU-side copy of its data
		float *pPos = apVtxBuffer->GetFloatArray(eVertexBufferElement_Position);
		float *pNormal = apVtxBuffer->GetFloatArray(eVertexBufferElement_Normal);
		float *pUv = apVtxBuffer->GetFloatArray(eVertexBufferElement_Texture0);
		int lVtxNum = apVtxBuffer->GetVertexNum();
		int lIdxNum = apVtxBuffer->GetIndexNum();
		if(pPos==NULL || lVtxNum <= 0 || lIdxNum <= 0) return NULL;

		int lPosStride = apVtxBuffer->GetElementNum(eVertexBufferElement_Position);
		int lNormalStride = pNormal ? apVtxBuffer->GetElementNum(eVertexBufferElement_Normal) : 0;
		int lUvStride = pUv ? apVtxBuffer->GetElementNum(eVertexBufferElement_Texture0) : 0;

		std::vector<remixapi_HardcodedVertex> vVertices(lVtxNum);
		for(int i=0; i<lVtxNum; ++i)
		{
			remixapi_HardcodedVertex &vtx = vVertices[i];
			vtx = remixapi_HardcodedVertex();
			for(int j=0; j<3; ++j) vtx.position[j] = pPos[i*lPosStride + j];
			if(pNormal) for(int j=0; j<3; ++j) vtx.normal[j] = pNormal[i*lNormalStride + j];
			if(pUv)
			{
				vtx.texcoord[0] = pUv[i*lUvStride + 0];
				vtx.texcoord[1] = gbFlipV ? 1.0f - pUv[i*lUvStride + 1] : pUv[i*lUvStride + 1];
			}
			vtx.color = 0xFFFFFFFF;
		}

		remixapi_MeshInfoSurfaceTriangles surface = {};
		surface.vertices_values = &vVertices[0];
		surface.vertices_count = (uint64_t)lVtxNum;
		surface.indices_values = (const uint32_t*)apVtxBuffer->GetIndices();
		surface.indices_count = (uint64_t)lIdxNum;
		surface.material = GetRemixMaterial(apMaterial);

		remixapi_MeshInfo meshInfo = {};
		meshInfo.sType = REMIXAPI_STRUCT_TYPE_MESH_INFO;
		meshInfo.hash = ((uint64_t)(uintptr_t)apVtxBuffer) * 31 + (uint64_t)(uintptr_t)apMaterial;
		meshInfo.surfaces_values = &surface;
		meshInfo.surfaces_count = 1;

		remixapi_ErrorCode lResult = gRemix.CreateMesh(&meshInfo, &hMesh);
		if(lResult != REMIXAPI_ERROR_CODE_SUCCESS)
		{
			Warning("Remix: CreateMesh failed (error %d)\n", lResult);
			hMesh = NULL;
		}

		gmapMeshes[key] = hMesh;
		return hMesh;
	}

	//-----------------------------------------------------------------------

	// HPL2 matrices are m[row][col] with the translation in column 3, the same
	// layout as Remix's 3x4 transform
	static remixapi_Transform ToRemixTransform(const cMatrixf *apMatrix)
	{
		remixapi_Transform transform = {};
		for(int row=0; row<3; ++row)
			for(int col=0; col<4; ++col)
				transform.matrix[row][col] = apMatrix ? apMatrix->m[row][col] : (row==col ? 1.0f : 0.0f);
		return transform;
	}

	//-----------------------------------------------------------------------

	static void DrawRenderable(iRenderable *apObject, cFrustum *apFrustum)
	{
		iVertexBuffer *pVtxBuffer = apObject->GetVertexBuffer();
		if(pVtxBuffer==NULL) return;

		remixapi_MeshHandle hMesh = GetRemixMesh(pVtxBuffer, apObject->GetMaterial());
		if(hMesh==NULL) return;

		remixapi_InstanceInfo instanceInfo = {};
		instanceInfo.sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO;
		instanceInfo.mesh = hMesh;
		instanceInfo.transform = ToRemixTransform(apObject->GetModelMatrix(apFrustum));
		instanceInfo.doubleSided = true;
		gRemix.DrawInstance(&instanceInfo);
	}

	//-----------------------------------------------------------------------

	static void DrawLight(iLight *apLight)
	{
		eLightType lightType = apLight->GetLightType();
		if(lightType != eLightType_Point && lightType != eLightType_Spot) return;	// box lights are ambient fill; Remix does GI

		const cColor &color = apLight->GetDiffuseColor();
		cVector3f vPos = apLight->GetWorldPosition();

		remixapi_LightInfoSphereEXT sphereInfo = {};
		sphereInfo.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
		sphereInfo.position = { vPos.x, vPos.y, vPos.z };
		sphereInfo.radius = 0.05f;
		sphereInfo.volumetricRadianceScale = 1.0f;

		if(lightType == eLightType_Spot)
		{
			// A spot light looks down its -Z axis, like a camera
			cLightSpot *pSpot = static_cast<cLightSpot*>(apLight);
			const cMatrixf &mtxWorld = pSpot->GetWorldMatrix();
			cVector3f vDir = cMath::Vector3Normalize(cVector3f(-mtxWorld.m[0][2], -mtxWorld.m[1][2], -mtxWorld.m[2][2]));

			sphereInfo.shaping_hasvalue = true;
			sphereInfo.shaping_value.direction = { vDir.x, vDir.y, vDir.z };
			sphereInfo.shaping_value.coneAngleDegrees = cMath::ToDeg(pSpot->GetFOV()) * 0.5f;
			sphereInfo.shaping_value.coneSoftness = 0.1f;
			sphereInfo.shaping_value.focusExponent = 0.0f;
		}

		remixapi_LightInfo lightInfo = {};
		lightInfo.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
		lightInfo.pNext = &sphereInfo;
		lightInfo.hash = (uint64_t)(uintptr_t)apLight;
		lightInfo.radiance = { color.r * gfLightScale, color.g * gfLightScale, color.b * gfLightScale };

		remixapi_LightHandle hLight = NULL;
		if(gRemix.CreateLight(&lightInfo, &hLight) == REMIXAPI_ERROR_CODE_SUCCESS)
		{
			gRemix.DrawLightInstance(hLight);
			gvFrameLights.push_back(hLight);
		}
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PUBLIC
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	void cRemixSpike::OnWorldRendered(iRenderer *apRenderer, cCamera *apCamera)
	{
		if(gbStartupTried==false) gbRunning = Startup();
		if(gbRunning==false || apRenderer==NULL || apCamera==NULL) return;

		// One Remix frame per engine frame, even with several viewports
		int lFrame = iRenderer::GetRenderFrameCount();
		if(lFrame == glLastFrame) return;
		glLastFrame = lFrame;

		cRenderList *pRenderList = apRenderer->GetCurrentRenderList();
		if(pRenderList==NULL) return;

		////////////////////////////////
		// Camera, from the engine camera's world-space basis
		cVector3f vPos = apCamera->GetPosition();
		cVector3f vForward = apCamera->GetForward();
		cVector3f vUp = apCamera->GetUp();
		cVector3f vRight = apCamera->GetRight();

		remixapi_CameraInfoParameterizedEXT cameraParams = {};
		cameraParams.sType = REMIXAPI_STRUCT_TYPE_CAMERA_INFO_PARAMETERIZED_EXT;
		cameraParams.position = { vPos.x, vPos.y, vPos.z };
		cameraParams.forward = { vForward.x, vForward.y, vForward.z };
		cameraParams.up = { vUp.x, vUp.y, vUp.z };
		cameraParams.right = { vRight.x, vRight.y, vRight.z };
		cameraParams.fovYInDegrees = cMath::ToDeg(apCamera->GetFOV());
		cameraParams.aspect = apCamera->GetAspect();
		cameraParams.nearPlane = apCamera->GetNearClipPlane();
		cameraParams.farPlane = apCamera->GetFarClipPlane();

		remixapi_CameraInfo cameraInfo = {};
		cameraInfo.sType = REMIXAPI_STRUCT_TYPE_CAMERA_INFO;
		cameraInfo.pNext = &cameraParams;
		cameraInfo.type = REMIXAPI_CAMERA_TYPE_WORLD;
		gRemix.SetupCamera(&cameraInfo);

		////////////////////////////////
		// Solid and translucent objects the OpenGL renderer drew this frame
		cFrustum *pFrustum = apCamera->GetFrustum();
		for(int i=0; i<pRenderList->GetSolidObjectNum(); ++i)
			DrawRenderable(pRenderList->GetSolidObject(i), pFrustum);
		for(int i=0; i<pRenderList->GetTransObjectNum(); ++i)
			DrawRenderable(pRenderList->GetTransObject(i), pFrustum);

		////////////////////////////////
		// Lights: recreate each frame, keeping each light's hash stable
		for(size_t i=0; i<gvFrameLights.size(); ++i) gRemix.DestroyLight(gvFrameLights[i]);
		gvFrameLights.clear();
		for(int i=0; i<pRenderList->GetLightNum(); ++i)
			DrawLight(pRenderList->GetLight(i));

		remixapi_PresentInfo presentInfo = {};
		presentInfo.sType = REMIXAPI_STRUCT_TYPE_PRESENT_INFO;
		gRemix.Present(&presentInfo);
	}

	//-----------------------------------------------------------------------
}
