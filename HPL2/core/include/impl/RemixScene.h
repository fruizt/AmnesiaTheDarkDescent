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

#include "system/SystemTypes.h"

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
	class cRemixScene
	{
	public:
		// Tuning: light color to Remix radiance, flipping texture V, normal maps
		cRemixScene(remixapi_Interface *apApi, float afLightScale, bool abFlipV, bool abNormalMaps);

		void SetCamera(cFrustum *apFrustum);
		// Destroys the previous frame's lights and dynamic meshes; call first
		void BeginFrame();
		// Draws one instance of the object this frame (meshes, billboards...).
		// Objects whose vertex buffer the engine rewrites (particles, skinned
		// meshes, billboards, beams, ropes) get a new mesh every frame; the
		// others' meshes are cached.
		void DrawObject(iRenderable *apObject, cFrustum *apFrustum);
		void DrawLight(iLight *apLight);

		// Everything cached is keyed by engine pointers, which a new world may
		// reuse: drop it all when a world is destroyed
		void ClearCaches();

		int GetMeshNum(){ return (int)m_mapMeshes.size(); }
		int GetMaterialNum(){ return (int)m_mapMaterials.size(); }
		// Cache entries found stale and rebuilt (should stay 0, see ClearCaches)
		int GetStaleMeshHits(){ return mlStaleMeshHits; }
		int GetStaleMaterialHits(){ return mlStaleMaterialHits; }

	private:
		typedef std::pair<iVertexBuffer*, cMaterial*> tMeshKey;

		remixapi_MaterialHandle GetMaterial(cMaterial *apMaterial);
		remixapi_MeshHandle GetMesh(iVertexBuffer *apVtxBuffer, cMaterial *apMaterial);
		remixapi_MeshHandle CreateMesh(iVertexBuffer *apVtxBuffer, remixapi_MaterialHandle ahMaterial, uint64_t alHash);
		void InvalidateMaterial(cMaterial *apMaterial);
		bool IsSkippedBillboard(iRenderable *apObject);

		remixapi_Interface *mpApi;
		float mfLightScale;
		bool mbFlipV;
		bool mbNormalMaps;

		std::map<cMaterial*, remixapi_MaterialHandle> m_mapMaterials;
		std::map<tMeshKey, remixapi_MeshHandle> m_mapMeshes;

		// What each cache entry was made from. A hit that doesn't match is rebuilt
		// instead of reused, and counted.
		std::map<cMaterial*, tWString> m_mapMaterialAlbedo;
		std::map<tMeshKey, std::pair<int,int> > m_mapMeshCounts;	// vertices, indices
		int mlStaleMeshHits;
		int mlStaleMaterialHits;

		std::vector<remixapi_LightHandle> mvFrameLights;
		std::vector<remixapi_MeshHandle> mvFrameMeshes;	// dynamic meshes, rebuilt every frame
	};

	//-----------------------------------------

};
#endif // HPL_REMIX_SCENE_H
