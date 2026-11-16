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

	// Remix loads textures from files, and only some formats: it rejects
	// Amnesia's .tga and .jpg ("Unsupported image file format"), which most
	// particles use. For those, the texture the engine already loaded into
	// OpenGL is read back (with its mipmaps) and written as an uncompressed
	// .dds into remix_texture_cache\ next to the exe; Remix gets that path. The
	// files are reused on later runs unless the source file is newer.
	class cRemixTextureCache
	{
	public:
		cRemixTextureCache();

		// The path to give Remix for this texture: its own file if Remix can
		// load it, else the cached .dds. Empty if there's nothing usable.
		// Needs the OpenGL context current.
		tWString GetPath(iTexture *apTexture);

	private:
		bool WriteDds(iTexture *apTexture, const tWString &asDestPath);

		tWString msCacheDir;
		std::map<tWString, tWString> m_mapConverted;	// source path -> cached path (or empty if it failed)
		int mlWritten;
	};

	//-----------------------------------------

};
#endif // HPL_REMIX_TEXTURE_CACHE_H
