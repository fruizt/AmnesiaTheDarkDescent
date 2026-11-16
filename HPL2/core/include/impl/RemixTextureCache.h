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

#ifndef HPL_REMIX_TEXTURE_CACHE_H
#define HPL_REMIX_TEXTURE_CACHE_H

#include "system/SystemTypes.h"

#include <map>

namespace hpl {

	//-----------------------------------------

	class iTexture;

	//-----------------------------------------

	// What a texture is used as in Remix, which decides whether and how it's
	// converted
	enum eRemixTexture
	{
		// As is. Converted only when Remix can't load the format (.tga, .jpg)
		eRemixTexture_Color,
		// From Amnesia's tangent-space normal map (RGB, read as normalize(rgb - 0.5)
		// like classic mode) to Remix's unsigned octahedral encoding (RG)
		eRemixTexture_Normal,
		// From Amnesia's specular map (R = intensity, G = power) to Remix's
		// perceptual roughness (R)
		eRemixTexture_Roughness,

		eRemixTexture_LastEnum
	};

	//-----------------------------------------

	// Remix loads textures from files. The texture the engine already loaded
	// into OpenGL is read back (with its mipmaps), converted when needed, and
	// written as an uncompressed .dds into remix_texture_cache\ next to the exe;
	// Remix gets that path. Needed for formats Remix rejects (Amnesia's .tga and
	// .jpg, "Unsupported image file format"), and for maps whose encoding
	// differs (normal, specular). Files are reused on later runs unless the
	// source file is newer.
	class cRemixTextureCache
	{
	public:
		cRemixTextureCache();

		// abFlipNormalY flips the normal maps' green channel (if bumps look lit
		// from the wrong side)
		void SetFlipNormalY(bool abX){ mbFlipNormalY = abX; }

		// The path to give Remix for this texture used as aKind: its own file if
		// Remix can load it as is, else the cached .dds. Empty if there's nothing
		// usable. Needs the OpenGL context current.
		tWString GetPath(iTexture *apTexture, eRemixTexture aKind = eRemixTexture_Color);

	private:
		bool WriteDds(iTexture *apTexture, const tWString &asDestPath, eRemixTexture aKind);
		void ConvertPixels(unsigned char *apBgra, size_t alPixels, eRemixTexture aKind);

		tWString msCacheDir;
		bool mbFlipNormalY;
		std::map<tWString, tWString> m_mapConverted[eRemixTexture_LastEnum];	// source path -> cached path (or empty if it failed)
		int mlWritten;
	};

	//-----------------------------------------

};
#endif // HPL_REMIX_TEXTURE_CACHE_H
