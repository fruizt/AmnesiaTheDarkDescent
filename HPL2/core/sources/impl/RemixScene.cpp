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
#include "system/String.h"

#include <cstring>

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

	// 64-bit FNV-1a: stable, simple hashing of bytes and names
	static const uint64_t kFnvOffset = 14695981039346656037ull;
	static const uint64_t kFnvPrime = 1099511628211ull;

	static uint64_t HashBytes(const void *apData, size_t alSize, uint64_t alHash = kFnvOffset)
	{
		const unsigned char *pBytes = static_cast<const unsigned char*>(apData);
		for(size_t i=0; i<alSize; ++i) { alHash ^= pBytes[i]; alHash *= kFnvPrime; }
		return alHash;
	}

	static uint64_t HashString(const tString &asText, uint64_t alHash = kFnvOffset)
	{
		return HashBytes(asText.c_str(), asText.size(), alHash);
	}

	// Combines two hashes (order matters)
	static uint64_t MixHash(uint64_t alA, uint64_t alB)
	{
		return HashBytes(&alB, sizeof(alB), alA);
	}

	//-----------------------------------------------------------------------

	// Effects that fake what path tracing does for real, left out of RTX mode
	// (they stay in classic mode). Matched against the material name.
	// - Light-ray billboards: Remix's volumetric lighting makes real light shafts.
	// - Area fog particles: large flat cards with a cloud texture, which classic
	//   mode fades where they meet geometry; path traced they show as lit,
	//   textured sheets. Remix's volumetric fog is to replace them.
	struct tSkippedEffect { eRenderableType mType; const char *msMaterialPart; };
	static const tSkippedEffect gvSkippedEffects[] = {
		{ eRenderableType_Billboard, "lightray" },
		{ eRenderableType_ParticleEmitter, "area_fog" },
	};

	// What the player holds (the lantern, its lights and glow) is part of one
	// entity in Amnesia; its lights and objects are named after it
	// ("PlayerHands_PointLight_1"). Distance to the camera can't tell: level
	// lights the player walks past are as close.
	static const char *gsHeldEntityPrefix = "PlayerHands_";

	static bool IsHeld(iEntity3D *apEntity)
	{
		return apEntity->GetName().compare(0, strlen(gsHeldEntityPrefix), gsHeldEntityPrefix) == 0;
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// CONSTRUCTORS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	cRemixScene::cRemixScene(remixapi_Interface *apApi, float afLightScale, float afHeldLightRadius, bool abFlipV, bool abNormalMaps, bool abLogHashes)
	{
		mpApi = apApi;
		mfLightScale = afLightScale;
		mfHeldLightRadius = afHeldLightRadius;
		mvCameraPos = cVector3f(0);
		mbFlipV = abFlipV;
		mbNormalMaps = abNormalMaps;
		mbLogHashes = abLogHashes;
		mlWorldHash = kFnvOffset;
		mlStaleMeshHits = 0;
		mlDuplicateNames = 0;

		// The game folder (the working directory), to make paths relative to it
		wchar_t sDir[MAX_PATH] = {};
		GetCurrentDirectoryW(MAX_PATH, sDir);
		msGameDir = cString::ToLowerCaseW(cString::ReplaceCharToW(sDir, _W("\\"), _W("/")));
		if(msGameDir.empty()==false && msGameDir[msGameDir.size()-1] != L'/') msGameDir += L'/';
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
		mvCameraPos = vPos;
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
		if(IsSkippedEffect(apObject)) return;

		iVertexBuffer *pVtxBuffer = apObject->GetVertexBuffer();
		if(pVtxBuffer==NULL) return;

		// Glowing objects near the camera: where held lights really are (DrawLight)
		cMaterial *pMaterial = apObject->GetMaterial();
		if(pMaterial && pMaterial->GetTexture(eMaterialTexture_Illumination) && IsHeld(apObject))
			mvGlowCentersNearCamera.push_back(apObject->GetBoundingVolume()->GetWorldCenter());

		remixapi_MeshHandle hMesh = NULL;
		if(pVtxBuffer->GetUsageType() == eVertexBufferUsageType_Static)
		{
			hMesh = GetMesh(pVtxBuffer, apObject->GetMaterial());
		}
		else
		{
			// Rewritten by the engine every frame: a cached copy would freeze its
			// first shape (that froze area fog particles into a "veil" in front of
			// the camera, and the hands in their first pose). Its shape can't
			// identify it, so: level, object name and material. Same hash every
			// frame, so Remix sees one object moving.
			uint64_t lHash = MixHash(GetNameHash(apObject->GetName(), m_mapObjectNamesThisFrame), GetMaterialHash(apObject->GetMaterial()));
			LogHashOnce(lHash, "dynamic mesh", apObject->GetName().c_str());
			hMesh = CreateMesh(pVtxBuffer, GetMaterial(apObject->GetMaterial()), lHash);
			if(hMesh) mvFrameMeshes.push_back(hMesh);
		}
		if(hMesh==NULL) return;

		remixapi_InstanceInfo instanceInfo = {};
		instanceInfo.sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO;
		instanceInfo.mesh = hMesh;
		instanceInfo.transform = ToRemixTransform(apObject->GetModelMatrix(apFrustum));
		instanceInfo.doubleSided = true;
		mpApi->DrawInstance(&instanceInfo);
	}

	//-----------------------------------------------------------------------

	void cRemixScene::BeginFrame(const tString &asWorldName)
	{
		// Lights are recreated every frame (as the Remix docs say to do for
		// changing lights), keeping each light's hash stable. So are dynamic meshes.
		for(size_t i=0; i<mvFrameLights.size(); ++i) mpApi->DestroyLight(mvFrameLights[i]);
		mvFrameLights.clear();
		for(size_t i=0; i<mvFrameMeshes.size(); ++i) mpApi->DestroyMesh(mvFrameMeshes[i]);
		mvFrameMeshes.clear();

		mlWorldHash = HashString(cString::ToLowerCase(asWorldName));
		m_mapLightNamesThisFrame.clear();
		mvGlowCentersNearCamera.clear();
		m_mapObjectNamesThisFrame.clear();
	}

	//-----------------------------------------------------------------------

	void cRemixScene::DrawLight(iLight *apLight)
	{
		eLightType lightType = apLight->GetLightType();
		if(lightType != eLightType_Point && lightType != eLightType_Spot) return;	// box lights are ambient fill; Remix does GI

		const cColor &color = apLight->GetDiffuseColor();
		cVector3f vPos = apLight->GetWorldPosition();
		float fRange = apLight->GetRadius();
		if(fRange <= 0) return;	// lights nothing in classic mode either

		////////////////////////////////
		// Brightness. Classic mode lights a surface at distance d with
		// color * (1 - d^2/R^2) (deferred_light_frag.glsl with the default linear
		// falloff map), never more than its color. Remix is physical: E = I/d^2.
		// Match the two at half the radius, where classic gives 0.75 * color:
		// I = 0.75 * color * (R/2)^2. So a light's power grows with its radius,
		// as the area it lights in classic mode does.
		float fIntensity = mfLightScale * 0.1875f * fRange * fRange;

		// Emitter size: a few centimeters, more for big lights (softer shadows)
		float fEmitterRadius = cMath::Clamp(fRange * 0.015f, 0.03f, 0.15f);

		// Lights held by the player (the lantern)
		bool bHeld = IsHeld(apLight);
		if(bHeld)
		{
			// The lantern also carries a light reaching only 0.3 m, right at the
			// hand: a classic-mode trick to give the hand a warm glow. Physically
			// it's a light a few centimeters from the skin, which blew the hand
			// out. Held lights that reach less than a meter are left out (0003).
			if(fRange < 1.0f) return;

			// The lantern's light is placed relative to the hands' origin, 10-20 cm
			// behind the lantern, inside the forearm (measured with HPL_REMIX_DIAG).
			// Classic mode can't show the difference; a path tracer lights the hand
			// from inside. The light comes from the glowing part instead: the
			// center of the nearest glow-mapped object near the camera (the
			// lantern's glass and flame).
			float fBestSqrDist = 0.6f * 0.6f;
			cVector3f vEnginePos = vPos;
			for(size_t i=0; i<mvGlowCentersNearCamera.size(); ++i)
			{
				float fSqrDist = cMath::Vector3DistSqr(mvGlowCentersNearCamera[i], vEnginePos);
				if(fSqrDist < fBestSqrDist) { fBestSqrDist = fSqrDist; vPos = mvGlowCentersNearCamera[i]; }
			}
			if(mbLogHashes && vPos != vEnginePos)
			{
				char sSource[256];
				sprintf_s(sSource, "%s, moved %.2f m to the glowing object", apLight->GetName().c_str(), sqrtf(fBestSqrDist));
				LogHashOnce(MixHash(HashString(apLight->GetName()), 0x48454c44ull), "held light", sSource);
			}
			fEmitterRadius = mfHeldLightRadius;
		}

		// Remix takes radiance: intensity per projected area of the sphere
		float fRadiance = fIntensity / (kPif * fEmitterRadius * fEmitterRadius);

		remixapi_LightInfoSphereEXT sphereInfo = {};
		sphereInfo.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
		sphereInfo.position = { vPos.x, vPos.y, vPos.z };
		sphereInfo.radius = fEmitterRadius;
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
		lightInfo.hash = GetNameHash(apLight->GetName(), m_mapLightNamesThisFrame);
		LogHashOnce(lightInfo.hash, "light", apLight->GetName().c_str());
		lightInfo.radiance = { color.r * fRadiance, color.g * fRadiance, color.b * fRadiance };

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
		for(std::map<uint64_t, remixapi_MeshHandle>::iterator it = m_mapMeshes.begin(); it != m_mapMeshes.end(); ++it)
			if(it->second) mpApi->DestroyMesh(it->second);
		for(std::map<uint64_t, remixapi_MaterialHandle>::iterator it = m_mapMaterials.begin(); it != m_mapMaterials.end(); ++it)
			if(it->second) mpApi->DestroyMaterial(it->second);

		Log("Remix: world destroyed, dropped %d cached meshes and %d materials\n", (int)m_mapMeshes.size(), (int)m_mapMaterials.size());
		m_mapMeshes.clear();
		m_mapMaterials.clear();
		m_mapGeometry.clear();
	}

	//-----------------------------------------------------------------------

	// The material file's path relative to the game folder, lower case with '/'
	// ("static_objects/castlebase/wall/castlebase_wall.mat"). Materials made in
	// code, without a file, use their name.
	uint64_t cRemixScene::GetMaterialHash(cMaterial *apMaterial)
	{
		if(apMaterial==NULL) return 0;

		const tWString &sPath = apMaterial->GetFullPath();
		if(sPath.empty()) return HashString("material:" + apMaterial->GetName());

		size_t lStart = 0;
		if(sPath.size() > msGameDir.size())
		{
			bool bInGameDir = true;
			for(size_t i=0; i<msGameDir.size() && bInGameDir; ++i)
			{
				wchar_t c = towlower(sPath[i]);
				if(c == L'\\') c = L'/';
				bInGameDir = c == msGameDir[i];
			}
			if(bInGameDir) lStart = msGameDir.size();
		}

		uint64_t lHash = kFnvOffset;
		for(size_t i=lStart; i<sPath.size(); ++i)
		{
			wchar_t c = towlower(sPath[i]);
			if(c == L'\\') c = L'/';
			unsigned short lChar = (unsigned short)c;
			lHash = HashBytes(&lChar, sizeof(lChar), lHash);
		}
		return lHash;
	}

	//-----------------------------------------------------------------------

	// The geometry as drawn: positions, normals, UVs and indices. Computed once
	// per vertex buffer (static buffers don't change).
	uint64_t cRemixScene::GetGeometryHash(iVertexBuffer *apVtxBuffer)
	{
		std::map<iVertexBuffer*, tGeometry>::iterator it = m_mapGeometry.find(apVtxBuffer);
		if(it != m_mapGeometry.end())
		{
			// A pointer reused by another buffer (should be caught by ClearCaches)
			if(it->second.mlVtxNum == apVtxBuffer->GetVertexNum() && it->second.mlIdxNum == apVtxBuffer->GetIndexNum())
				return it->second.mlHash;
			++mlStaleMeshHits;
		}

		// Independent of triangle and vertex order: the static geometry the engine
		// combines at load time ("CombinedObjects") comes out in a different
		// order on every run. Each triangle is hashed from its three vertices
		// (only the components sent to Remix: position xyz, normal xyz, UV xy;
		// position's w isn't always initialized), and the triangle hashes are
		// summed, so any order gives the same result.
		int lVtxNum = apVtxBuffer->GetVertexNum();
		int lIdxNum = apVtxBuffer->GetIndexNum();
		unsigned int *pIndices = apVtxBuffer->GetIndices();

		const eVertexBufferElement vElements[] = { eVertexBufferElement_Position, eVertexBufferElement_Normal, eVertexBufferElement_Texture0 };
		const int vUsedComponents[] = { 3, 3, 2 };
		float *vData[3];
		int vStride[3], vUsed[3];
		for(int i=0; i<3; ++i)
		{
			vData[i] = apVtxBuffer->GetFloatArray(vElements[i]);
			vStride[i] = vData[i] ? apVtxBuffer->GetElementNum(vElements[i]) : 0;
			vUsed[i] = cMath::Min(vUsedComponents[i], vStride[i]);
		}

		uint64_t lSum = 0;
		int lTriNum = pIndices ? lIdxNum / 3 : 0;
		for(int t=0; t<lTriNum; ++t)
		{
			uint64_t lTriHash = kFnvOffset;
			for(int c=0; c<3; ++c)
			{
				unsigned int lVtx = pIndices[t*3 + c];
				if((int)lVtx >= lVtxNum) continue;
				for(int i=0; i<3; ++i)
					if(vData[i]) lTriHash = HashBytes(&vData[i][lVtx * vStride[i]], sizeof(float) * vUsed[i], lTriHash);
			}
			lSum += lTriHash;
		}
		uint64_t lHash = MixHash(MixHash(kFnvOffset, lSum), (uint64_t)lTriNum);

		tGeometry geometry;
		geometry.mlHash = lHash;
		geometry.mlVtxNum = lVtxNum;
		geometry.mlIdxNum = apVtxBuffer->GetIndexNum();
		m_mapGeometry[apVtxBuffer] = geometry;
		return lHash;
	}

	//-----------------------------------------------------------------------

	// Level and name. A name used twice in the same frame gets a counter, and is
	// counted: such hashes depend on draw order and may change between runs.
	uint64_t cRemixScene::GetNameHash(const tString &asName, std::map<uint64_t, int> &a_mapUsedThisFrame)
	{
		uint64_t lHash = HashString(cString::ToLowerCase(asName), mlWorldHash);
		int &lUses = a_mapUsedThisFrame[lHash];
		++lUses;
		if(lUses == 1) return lHash;

		if(lUses == 2) ++mlDuplicateNames;
		return MixHash(lHash, (uint64_t)lUses);
	}

	//-----------------------------------------------------------------------

	void cRemixScene::LogHashOnce(uint64_t alHash, const char *asKind, const char *asSource)
	{
		if(mbLogHashes==false) return;
		bool &bLogged = m_mapLoggedHashes[alHash];
		if(bLogged) return;
		bLogged = true;
		Log("Remix hash %016llx %s '%s'\n", (unsigned long long)alHash, asKind, asSource);
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PRIVATE METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	remixapi_MaterialHandle cRemixScene::GetMaterial(cMaterial *apMaterial)
	{
		if(apMaterial==NULL) return NULL;

		// Keyed by what the material is (its file), not where it is in memory
		uint64_t lMaterialHash = GetMaterialHash(apMaterial);
		std::map<uint64_t, remixapi_MaterialHandle>::iterator it = m_mapMaterials.find(lMaterialHash);
		if(it != m_mapMaterials.end()) return it->second;

		// Amnesia's texture files, passed by path. Converted first when Remix
		// can't load the format, and always for the normal map (to Remix's
		// octahedral encoding) and the specular map (to roughness).
		tWString sAlbedo, sNormal, sEmissive, sRoughness;
		iTexture *pTexture = apMaterial->GetTexture(eMaterialTexture_Diffuse);
		sAlbedo = mTextureCache.GetPath(pTexture);
		pTexture = apMaterial->GetTexture(eMaterialTexture_NMap);
		if(mbNormalMaps) sNormal = mTextureCache.GetPath(pTexture, eRemixTexture_Normal);
		pTexture = apMaterial->GetTexture(eMaterialTexture_Specular);
		sRoughness = mTextureCache.GetPath(pTexture, eRemixTexture_Roughness);
		pTexture = apMaterial->GetTexture(eMaterialTexture_Illumination);
		sEmissive = mTextureCache.GetPath(pTexture);

		// Defaults from the Remix C++ wrapper (remix.h), opaque, no alpha test
		remixapi_MaterialInfoOpaqueEXT opaqueInfo = {};
		opaqueInfo.sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT;
		opaqueInfo.albedoConstant = { 0.8f, 0.8f, 0.8f };
		opaqueInfo.opacityConstant = 1.0f;
		// Without a specular map classic mode shows no highlight at all: fully
		// rough. (A middle value everywhere was much of the "plastic" look.)
		opaqueInfo.roughnessTexture = sRoughness.empty() ? NULL : sRoughness.c_str();
		opaqueInfo.roughnessConstant = 1.0f;
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
		materialInfo.hash = lMaterialHash;
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

		m_mapMaterials[lMaterialHash] = hMaterial;	// also remembers failures (NULL), so they're tried once
		LogHashOnce(lMaterialHash, "material", cString::To8Char(apMaterial->GetFullPath()).c_str());
		return hMaterial;
	}

	//-----------------------------------------------------------------------

	remixapi_MeshHandle cRemixScene::GetMesh(iVertexBuffer *apVtxBuffer, cMaterial *apMaterial)
	{
		// Geometry and material: identical objects (a crate placed 20 times)
		// share one mesh, drawn as 20 instances
		uint64_t lHash = MixHash(GetGeometryHash(apVtxBuffer), GetMaterialHash(apMaterial));
		std::map<uint64_t, remixapi_MeshHandle>::iterator it = m_mapMeshes.find(lHash);
		if(it != m_mapMeshes.end()) return it->second;

		remixapi_MeshHandle hMesh = CreateMesh(apVtxBuffer, GetMaterial(apMaterial), lHash);
		m_mapMeshes[lHash] = hMesh;	// also remembers failures (NULL), so they're tried once
		if(mbLogHashes)
		{
			char sSource[512];
			sprintf_s(sSource, "%d vertices, %d indices, material %s", apVtxBuffer->GetVertexNum(), apVtxBuffer->GetIndexNum(),
						apMaterial ? apMaterial->GetName().c_str() : "-");
			LogHashOnce(lHash, "mesh", sSource);
		}
		return hMesh;
	}

	//-----------------------------------------------------------------------

	// A Remix mesh from the vertex buffer's CPU-side copy of its data
	remixapi_MeshHandle cRemixScene::CreateMesh(iVertexBuffer *apVtxBuffer, remixapi_MaterialHandle ahMaterial, uint64_t alHash)
	{
		float *pPos = apVtxBuffer->GetFloatArray(eVertexBufferElement_Position);
		float *pNormal = apVtxBuffer->GetFloatArray(eVertexBufferElement_Normal);
		float *pUv = apVtxBuffer->GetFloatArray(eVertexBufferElement_Texture0);
		float *pColor = apVtxBuffer->GetFloatArray(eVertexBufferElement_Color0);
		unsigned int *pIndices = apVtxBuffer->GetIndices();

		// Only the indices drawn (particles fill part of a buffer sized for the
		// maximum), and only the vertices those reach
		int lIdxNum = apVtxBuffer->GetIndexNum();
		if(apVtxBuffer->GetElementNum() >= 0 && apVtxBuffer->GetElementNum() < lIdxNum) lIdxNum = apVtxBuffer->GetElementNum();
		if(pPos==NULL || pIndices==NULL || lIdxNum <= 0) return NULL;

		int lVtxNum = 0;
		for(int i=0; i<lIdxNum; ++i) if((int)pIndices[i] + 1 > lVtxNum) lVtxNum = (int)pIndices[i] + 1;
		if(lVtxNum > apVtxBuffer->GetVertexNum()) return NULL;

		int lPosStride = apVtxBuffer->GetElementNum(eVertexBufferElement_Position);
		int lNormalStride = pNormal ? apVtxBuffer->GetElementNum(eVertexBufferElement_Normal) : 0;
		int lUvStride = pUv ? apVtxBuffer->GetElementNum(eVertexBufferElement_Texture0) : 0;
		int lColorStride = pColor ? apVtxBuffer->GetElementNum(eVertexBufferElement_Color0) : 0;

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

			// Vertex color (particles fade through it), packed as D3D's ARGB
			vtx.color = 0xFFFFFFFF;
			if(pColor && lColorStride >= 4)
			{
				const float *pC = &pColor[i*lColorStride];
				unsigned int lR = (unsigned int)(cMath::Clamp(pC[0], 0.0f, 1.0f) * 255.0f + 0.5f);
				unsigned int lG = (unsigned int)(cMath::Clamp(pC[1], 0.0f, 1.0f) * 255.0f + 0.5f);
				unsigned int lB = (unsigned int)(cMath::Clamp(pC[2], 0.0f, 1.0f) * 255.0f + 0.5f);
				unsigned int lA = (unsigned int)(cMath::Clamp(pC[3], 0.0f, 1.0f) * 255.0f + 0.5f);
				vtx.color = (lA << 24) | (lR << 16) | (lG << 8) | lB;
			}
		}

		remixapi_MeshInfoSurfaceTriangles surface = {};
		surface.vertices_values = &vVertices[0];
		surface.vertices_count = (uint64_t)lVtxNum;
		surface.indices_values = (const uint32_t*)pIndices;
		surface.indices_count = (uint64_t)lIdxNum;
		surface.material = ahMaterial;

		remixapi_MeshInfo meshInfo = {};
		meshInfo.sType = REMIXAPI_STRUCT_TYPE_MESH_INFO;
		meshInfo.hash = alHash;
		meshInfo.surfaces_values = &surface;
		meshInfo.surfaces_count = 1;

		remixapi_MeshHandle hMesh = NULL;
		remixapi_ErrorCode lResult = mpApi->CreateMesh(&meshInfo, &hMesh);
		if(lResult != REMIXAPI_ERROR_CODE_SUCCESS)
		{
			Warning("Remix: CreateMesh failed (error %d)\n", lResult);
			hMesh = NULL;
		}
		return hMesh;
	}

	//-----------------------------------------------------------------------

	bool cRemixScene::IsSkippedEffect(iRenderable *apObject)
	{
		cMaterial *pMaterial = apObject->GetMaterial();
		if(pMaterial==NULL) return false;

		const tString &sName = pMaterial->GetName();
		for(size_t i=0; i<sizeof(gvSkippedEffects)/sizeof(gvSkippedEffects[0]); ++i)
		{
			if(	apObject->GetRenderType() == gvSkippedEffects[i].mType &&
				sName.find(gvSkippedEffects[i].msMaterialPart) != tString::npos) return true;
		}
		return false;
	}

	//-----------------------------------------------------------------------
}
