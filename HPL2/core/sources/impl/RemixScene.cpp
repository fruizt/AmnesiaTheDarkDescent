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

#include "graphics/Renderable.h"
#include "graphics/VertexBuffer.h"
#include "graphics/Material.h"
#include "graphics/MaterialType.h"
#include "graphics/MaterialType_BasicTranslucent.h"
#include "graphics/Texture.h"
#include "math/Frustum.h"
#include "math/Math.h"
#include "scene/Light.h"
#include "scene/LightSpot.h"
#include "system/LowLevelSystem.h"

namespace hpl {

	//////////////////////////////////////////////////////////////////////////
	// HELPERS
	//////////////////////////////////////////////////////////////////////////

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

	// Billboards that fake lighting effects which path tracing produces for real
	// (Remix's volumetric lighting makes actual light shafts). Matched against
	// the material name. They stay in the OpenGL renderer (classic mode).
	static const char *gvSkippedBillboardMaterials[] = { "lightray" };

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// CONSTRUCTORS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	cRemixScene::cRemixScene(remixapi_Interface *apApi, float afLightScale, bool abFlipV, bool abNormalMaps)
	{
		mpApi = apApi;
		mfLightScale = afLightScale;
		mbFlipV = abFlipV;
		mbNormalMaps = abNormalMaps;
		mlStaleMeshHits = 0;
		mlStaleMaterialHits = 0;
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PUBLIC METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	void cRemixScene::SetCamera(cFrustum *apFrustum)
	{
		// The camera's world-space basis, from the view matrix (as cCamera does)
		const cMatrixf &mtxView = apFrustum->GetViewMatrix();
		cVector3f vPos = apFrustum->GetOrigin();
		cVector3f vForward = mtxView.GetForward() * -1.0f;
		cVector3f vUp = mtxView.GetUp();
		cVector3f vRight = mtxView.GetRight();

		remixapi_CameraInfoParameterizedEXT cameraParams = {};
		cameraParams.sType = REMIXAPI_STRUCT_TYPE_CAMERA_INFO_PARAMETERIZED_EXT;
		cameraParams.position = { vPos.x, vPos.y, vPos.z };
		cameraParams.forward = { vForward.x, vForward.y, vForward.z };
		cameraParams.up = { vUp.x, vUp.y, vUp.z };
		cameraParams.right = { vRight.x, vRight.y, vRight.z };
		cameraParams.fovYInDegrees = cMath::ToDeg(apFrustum->GetFOV());
		cameraParams.aspect = apFrustum->GetAspect();
		cameraParams.nearPlane = apFrustum->GetNearPlane();
		cameraParams.farPlane = apFrustum->GetFarPlane();

		remixapi_CameraInfo cameraInfo = {};
		cameraInfo.sType = REMIXAPI_STRUCT_TYPE_CAMERA_INFO;
		cameraInfo.pNext = &cameraParams;
		cameraInfo.type = REMIXAPI_CAMERA_TYPE_WORLD;
		mpApi->SetupCamera(&cameraInfo);
	}

	//-----------------------------------------------------------------------

	void cRemixScene::DrawObject(iRenderable *apObject, cFrustum *apFrustum)
	{
		if(IsSkippedBillboard(apObject)) return;

		iVertexBuffer *pVtxBuffer = apObject->GetVertexBuffer();
		if(pVtxBuffer==NULL) return;

		remixapi_MeshHandle hMesh = GetMesh(pVtxBuffer, apObject->GetMaterial());
		if(hMesh==NULL) return;

		remixapi_InstanceInfo instanceInfo = {};
		instanceInfo.sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO;
		instanceInfo.mesh = hMesh;
		instanceInfo.transform = ToRemixTransform(apObject->GetModelMatrix(apFrustum));
		instanceInfo.doubleSided = true;
		mpApi->DrawInstance(&instanceInfo);
	}

	//-----------------------------------------------------------------------

	void cRemixScene::BeginLights()
	{
		// Lights are recreated every frame (as the Remix docs say to do for
		// changing lights), keeping each light's hash stable
		for(size_t i=0; i<mvFrameLights.size(); ++i) mpApi->DestroyLight(mvFrameLights[i]);
		mvFrameLights.clear();
	}

	//-----------------------------------------------------------------------

	void cRemixScene::DrawLight(iLight *apLight)
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
		lightInfo.radiance = { color.r * mfLightScale, color.g * mfLightScale, color.b * mfLightScale };

		remixapi_LightHandle hLight = NULL;
		if(mpApi->CreateLight(&lightInfo, &hLight) == REMIXAPI_ERROR_CODE_SUCCESS)
		{
			mpApi->DrawLightInstance(hLight);
			mvFrameLights.push_back(hLight);
		}
	}

	//-----------------------------------------------------------------------

	void cRemixScene::ClearCaches()
	{
		// Meshes first, they reference materials
		for(std::map<tMeshKey, remixapi_MeshHandle>::iterator it = m_mapMeshes.begin(); it != m_mapMeshes.end(); ++it)
			if(it->second) mpApi->DestroyMesh(it->second);
		for(std::map<cMaterial*, remixapi_MaterialHandle>::iterator it = m_mapMaterials.begin(); it != m_mapMaterials.end(); ++it)
			if(it->second) mpApi->DestroyMaterial(it->second);

		Log("Remix: world destroyed, dropped %d cached meshes and %d materials\n", (int)m_mapMeshes.size(), (int)m_mapMaterials.size());
		m_mapMeshes.clear();
		m_mapMeshCounts.clear();
		m_mapMaterials.clear();
		m_mapMaterialAlbedo.clear();
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PRIVATE METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	remixapi_MaterialHandle cRemixScene::GetMaterial(cMaterial *apMaterial)
	{
		if(apMaterial==NULL) return NULL;

		std::map<cMaterial*, remixapi_MaterialHandle>::iterator it = m_mapMaterials.find(apMaterial);
		if(it != m_mapMaterials.end())
		{
			// Safety net: reuse only if the entry was made from this material
			iTexture *pDiffuse = apMaterial->GetTexture(eMaterialTexture_Diffuse);
			tWString sNow = pDiffuse ? pDiffuse->GetFullPath() : tWString();
			if(sNow == m_mapMaterialAlbedo[apMaterial]) return it->second;

			++mlStaleMaterialHits;
			InvalidateMaterial(apMaterial);	// then rebuild below
		}

		// Amnesia's texture files, passed by path
		tWString sAlbedo, sNormal, sEmissive;
		iTexture *pTexture = apMaterial->GetTexture(eMaterialTexture_Diffuse);
		if(pTexture) sAlbedo = pTexture->GetFullPath();
		pTexture = apMaterial->GetTexture(eMaterialTexture_NMap);
		if(pTexture && mbNormalMaps) sNormal = pTexture->GetFullPath();
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
		remixapi_ErrorCode lResult = mpApi->CreateMaterial(&materialInfo, &hMaterial);
		if(lResult != REMIXAPI_ERROR_CODE_SUCCESS)
			Warning("Remix: CreateMaterial failed for '%s' (error %d)\n", apMaterial->GetName().c_str(), lResult);

		m_mapMaterials[apMaterial] = hMaterial;
		m_mapMaterialAlbedo[apMaterial] = sAlbedo;
		return hMaterial;
	}

	//-----------------------------------------------------------------------

	remixapi_MeshHandle cRemixScene::GetMesh(iVertexBuffer *apVtxBuffer, cMaterial *apMaterial)
	{
		// Material first: if its cache entry was stale, this also drops the meshes
		// that used it, so the lookup below can't return one of those
		remixapi_MaterialHandle hMaterial = GetMaterial(apMaterial);

		tMeshKey key(apVtxBuffer, apMaterial);
		std::map<tMeshKey, remixapi_MeshHandle>::iterator it = m_mapMeshes.find(key);
		if(it != m_mapMeshes.end())
		{
			// Safety net: reuse only if the entry was made from a buffer of this size
			const std::pair<int,int> &cached = m_mapMeshCounts[key];
			if(cached.first == apVtxBuffer->GetVertexNum() && cached.second == apVtxBuffer->GetIndexNum())
				return it->second;

			++mlStaleMeshHits;
			if(it->second) mpApi->DestroyMesh(it->second);	// then rebuild below
			m_mapMeshes.erase(it);
		}

		remixapi_MeshHandle hMesh = NULL;
		m_mapMeshes[key] = NULL;	// also remembers failures, so they're tried once
		m_mapMeshCounts[key] = std::pair<int,int>(apVtxBuffer->GetVertexNum(), apVtxBuffer->GetIndexNum());

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
				vtx.texcoord[1] = mbFlipV ? 1.0f - pUv[i*lUvStride + 1] : pUv[i*lUvStride + 1];
			}
			vtx.color = 0xFFFFFFFF;
		}

		remixapi_MeshInfoSurfaceTriangles surface = {};
		surface.vertices_values = &vVertices[0];
		surface.vertices_count = (uint64_t)lVtxNum;
		surface.indices_values = (const uint32_t*)apVtxBuffer->GetIndices();
		surface.indices_count = (uint64_t)lIdxNum;
		surface.material = hMaterial;

		remixapi_MeshInfo meshInfo = {};
		meshInfo.sType = REMIXAPI_STRUCT_TYPE_MESH_INFO;
		meshInfo.hash = ((uint64_t)(uintptr_t)apVtxBuffer) * 31 + (uint64_t)(uintptr_t)apMaterial;
		meshInfo.surfaces_values = &surface;
		meshInfo.surfaces_count = 1;

		remixapi_ErrorCode lResult = mpApi->CreateMesh(&meshInfo, &hMesh);
		if(lResult != REMIXAPI_ERROR_CODE_SUCCESS)
		{
			Warning("Remix: CreateMesh failed (error %d)\n", lResult);
			hMesh = NULL;
		}

		m_mapMeshes[key] = hMesh;
		return hMesh;
	}

	//-----------------------------------------------------------------------

	// Drop a material's Remix resources, and every cached mesh that uses it
	void cRemixScene::InvalidateMaterial(cMaterial *apMaterial)
	{
		std::map<tMeshKey, remixapi_MeshHandle>::iterator meshIt = m_mapMeshes.begin();
		while(meshIt != m_mapMeshes.end())
		{
			if(meshIt->first.second == apMaterial)
			{
				if(meshIt->second) mpApi->DestroyMesh(meshIt->second);
				m_mapMeshCounts.erase(meshIt->first);
				meshIt = m_mapMeshes.erase(meshIt);
			}
			else ++meshIt;
		}

		std::map<cMaterial*, remixapi_MaterialHandle>::iterator matIt = m_mapMaterials.find(apMaterial);
		if(matIt != m_mapMaterials.end())
		{
			if(matIt->second) mpApi->DestroyMaterial(matIt->second);
			m_mapMaterials.erase(matIt);
		}
		m_mapMaterialAlbedo.erase(apMaterial);
	}

	//-----------------------------------------------------------------------

	bool cRemixScene::IsSkippedBillboard(iRenderable *apObject)
	{
		if(apObject->GetRenderType() != eRenderableType_Billboard) return false;
		cMaterial *pMaterial = apObject->GetMaterial();
		if(pMaterial==NULL) return false;

		const tString &sName = pMaterial->GetName();
		for(size_t i=0; i<sizeof(gvSkippedBillboardMaterials)/sizeof(gvSkippedBillboardMaterials[0]); ++i)
			if(sName.find(gvSkippedBillboardMaterials[i]) != tString::npos) return true;
		return false;
	}

	//-----------------------------------------------------------------------
}
