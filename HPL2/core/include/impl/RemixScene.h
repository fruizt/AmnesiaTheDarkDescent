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

#ifndef HPL_REMIX_SCENE_H
#define HPL_REMIX_SCENE_H

#include "impl/RemixContext.h"	// remix_c.h (with windows.h)
#include "impl/RemixTextureCache.h"

#include "system/SystemTypes.h"
#include "math/MathTypes.h"

#include <map>
#include <utility>
#include <vector>

namespace hpl {

	//-----------------------------------------

	class iRenderable;
	class iVertexBuffer;
	class cMaterial;
	class iLight;
	class cFrustum;

	//-----------------------------------------

	// Translates engine objects into Remix resources (docs/decisions/0002):
	// meshes, materials, lights and the camera. Meshes and materials are created
	// once and cached; lights are recreated every frame.
	//
	// Every Remix resource gets a stable hash (docs/decisions/0003), the same on
	// every run and machine, since Remix's replacement workflow (Toolkit, mods)
	// keys everything by it:
	// - material: its file path, relative to the game folder;
	// - static mesh: its geometry (positions, normals, UVs, indices) and material;
	// - dynamic mesh (particles, skinned meshes, billboards...): level, object
	//   name and material, since its shape changes every frame;
	// - light: level and light name.
	class cRemixScene
	{
	public:
		// Tuning: light brightness (1 = matches classic mode at half a light's
		// radius), emitter radius of hand-held lights, flipping texture V,
		// normal maps. abLogHashes logs each hash once, with what it's made from.
		cRemixScene(remixapi_Interface *apApi, float afLightScale, float afHeldLightRadius, bool abFlipV, bool abNormalMaps, bool abLogHashes);

		void SetCamera(cFrustum *apFrustum);
		// Destroys the previous frame's lights and dynamic meshes; call first.
		// The world's name goes into the hashes of lights and dynamic meshes.
		void BeginFrame(const tString &asWorldName);
		// Draws one instance of the object this frame (meshes, billboards...).
		// Objects whose vertex buffer the engine rewrites (particles, skinned
		// meshes, billboards, beams, ropes) get a new mesh every frame; the
		// others' meshes are cached.
		void DrawObject(iRenderable *apObject, cFrustum *apFrustum);
		void DrawLight(iLight *apLight);

		// The geometry fingerprints are keyed by vertex buffer pointers, which a
		// new world may reuse: drop everything when a world is destroyed
		void ClearCaches();

		int GetMeshNum(){ return (int)m_mapMeshes.size(); }
		int GetMaterialNum(){ return (int)m_mapMaterials.size(); }
		// Vertex buffers whose pointer was reused by another buffer and got
		// fingerprinted again (should stay 0, see ClearCaches)
		int GetStaleMeshHits(){ return mlStaleMeshHits; }
		// Names used twice in one frame (lights, dynamic meshes); their hashes
		// get a counter and may change between runs
		int GetDuplicateNameNum(){ return mlDuplicateNames; }

	private:
		struct tGeometry
		{
			uint64_t mlHash;
			int mlVtxNum;
			int mlIdxNum;
		};

		uint64_t GetMaterialHash(cMaterial *apMaterial);
		uint64_t GetGeometryHash(iVertexBuffer *apVtxBuffer);
		uint64_t GetNameHash(const tString &asName, std::map<uint64_t, int> &a_mapUsedThisFrame);
		void LogHashOnce(uint64_t alHash, const char *asKind, const char *asSource);

		remixapi_MaterialHandle GetMaterial(cMaterial *apMaterial);
		remixapi_MeshHandle GetMesh(iVertexBuffer *apVtxBuffer, cMaterial *apMaterial);
		remixapi_MeshHandle CreateMesh(iVertexBuffer *apVtxBuffer, remixapi_MaterialHandle ahMaterial, uint64_t alHash);
		bool IsSkippedEffect(iRenderable *apObject);

		remixapi_Interface *mpApi;
		float mfLightScale;
		float mfHeldLightRadius;
		cVector3f mvCameraPos;
		bool mbFlipV;
		bool mbNormalMaps;
		bool mbLogHashes;

		tWString msGameDir;	// lower case, '/' separators, ends with '/'
		uint64_t mlWorldHash;

		cRemixTextureCache mTextureCache;
		std::map<uint64_t, remixapi_MaterialHandle> m_mapMaterials;	// by material hash
		std::map<uint64_t, remixapi_MeshHandle> m_mapMeshes;			// by geometry + material hash
		std::map<iVertexBuffer*, tGeometry> m_mapGeometry;
		int mlStaleMeshHits;
		int mlDuplicateNames;

		std::map<uint64_t, int> m_mapLightNamesThisFrame;
		std::map<uint64_t, int> m_mapObjectNamesThisFrame;
		std::map<uint64_t, bool> m_mapLoggedHashes;

		std::vector<remixapi_LightHandle> mvFrameLights;
		std::vector<remixapi_MeshHandle> mvFrameMeshes;	// dynamic meshes, rebuilt every frame
	};

	//-----------------------------------------

};
#endif // HPL_REMIX_SCENE_H
