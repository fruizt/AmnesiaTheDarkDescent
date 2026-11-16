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

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <GL/glew.h>
#include <windows.h>

#include "impl/RemixTextureCache.h"

#include "impl/SDLTexture.h"
#include "system/LowLevelSystem.h"
#include "system/String.h"
#include "math/Math.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace hpl {

	//////////////////////////////////////////////////////////////////////////
	// HELPERS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	// Last write time of a file, 0 if it doesn't exist
	static unsigned long long FileWriteTime(const tWString &asPath)
	{
		WIN32_FILE_ATTRIBUTE_DATA data;
		if(GetFileAttributesExW(asPath.c_str(), GetFileExInfoStandard, &data)==FALSE) return 0;
		return ((unsigned long long)data.ftLastWriteTime.dwHighDateTime << 32) | data.ftLastWriteTime.dwLowDateTime;
	}

	//-----------------------------------------------------------------------

	// FNV-1a, to keep cached names of same-named files in different folders apart
	static unsigned int HashPath(const tWString &asPath)
	{
		unsigned int lHash = 2166136261u;
		for(size_t i=0; i<asPath.size(); ++i)
		{
			lHash ^= (unsigned int)towlower(asPath[i]);
			lHash *= 16777619u;
		}
		return lHash;
	}

	//-----------------------------------------------------------------------

	// The DDS file header (after the "DDS " magic), uncompressed 32-bit BGRA
	struct tDdsHeader
	{
		unsigned int mlSize, mlFlags, mlHeight, mlWidth, mlPitch, mlDepth, mlMipMapCount;
		unsigned int mvReserved1[11];
		unsigned int mlPfSize, mlPfFlags, mlPfFourCC, mlPfBitCount, mlPfRMask, mlPfGMask, mlPfBMask, mlPfAMask;
		unsigned int mlCaps, mlCaps2, mlCaps3, mlCaps4, mlReserved2;
	};
	static_assert(sizeof(tDdsHeader) == 124, "the DDS header is 124 bytes");

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// CONSTRUCTORS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	cRemixTextureCache::cRemixTextureCache()
	{
		mlWritten = 0;
		mbFlipNormalY = false;

		// Next to the exe: ours, never the game's folder
		wchar_t sPath[MAX_PATH];
		GetModuleFileNameW(NULL, sPath, MAX_PATH);
		wchar_t *pFileName = wcsrchr(sPath, L'\\');
		if(pFileName) pFileName[1] = 0;
		msCacheDir = tWString(sPath) + L"remix_texture_cache\\";
		CreateDirectoryW(msCacheDir.c_str(), NULL);
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PUBLIC METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	tWString cRemixTextureCache::GetPath(iTexture *apTexture, eRemixTexture aKind)
	{
		if(apTexture==NULL) return tWString();

		const tWString &sSource = apTexture->GetFullPath();
		if(sSource.empty()) return tWString();

		tWString sExt = cString::ToLowerCaseW(cString::GetFileExtW(sSource));
		if(aKind == eRemixTexture_Color && sExt == L"dds") return sSource;	// Remix loads those itself

		std::map<tWString, tWString> &mapConverted = m_mapConverted[aKind];
		std::map<tWString, tWString>::iterator it = mapConverted.find(sSource);
		if(it != mapConverted.end()) return it->second;

		// "ps_area_fog_1a2b3c4d.dds", "castlebase_wall_nrm_1a2b3c4d_normal.dds"
		static const wchar_t *vKindSuffix[eRemixTexture_LastEnum] = { L"", L"_normal", L"_roughness" };
		tWString sName = cString::GetFileNameW(sSource);
		size_t lDot = sName.find_last_of(L'.');
		if(lDot != tWString::npos) sName = sName.substr(0, lDot);
		wchar_t sSuffix[48];
		swprintf_s(sSuffix, L"_%08x%ls%ls.dds", HashPath(sSource), vKindSuffix[aKind],
					aKind == eRemixTexture_Normal && mbFlipNormalY ? L"_flipy" : L"");
		tWString sDest = msCacheDir + sName + sSuffix;

		unsigned long long lDestTime = FileWriteTime(sDest);
		if(lDestTime == 0 || lDestTime < FileWriteTime(sSource))
		{
			if(WriteDds(apTexture, sDest, aKind))
			{
				++mlWritten;
				Log("Remix: converted texture '%ls' to '%ls'\n", sSource.c_str(), sDest.c_str());
			}
			else
			{
				Warning("Remix: could not convert texture '%ls' for Remix\n", sSource.c_str());
				sDest = tWString();
			}
		}

		mapConverted[sSource] = sDest;
		return sDest;
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PRIVATE METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	// Pixels are BGRA bytes
	void cRemixTextureCache::ConvertPixels(unsigned char *apBgra, size_t alPixels, eRemixTexture aKind)
	{
		if(aKind == eRemixTexture_Color) return;

		for(size_t i=0; i<alPixels; ++i)
		{
			unsigned char *pPixel = &apBgra[i*4];
			float fR = pPixel[2] / 255.0f, fG = pPixel[1] / 255.0f, fB = pPixel[0] / 255.0f;

			if(aKind == eRemixTexture_Normal)
			{
				// Classic mode: normalize(rgb - 0.5) in tangent space (z out of
				// the surface). Remix: unsigned octahedral, its exact encoder
				// (hemisphereDirectionToUnsignedOctahedral, packing.slangh).
				float fX = fR - 0.5f, fY = fG - 0.5f, fZ = fB - 0.5f;
				if(mbFlipNormalY) fY = -fY;
				if(fZ < 0.0f) fZ = 0.0f;	// a hemisphere: nothing points into the surface
				float fSum = fabsf(fX) + fabsf(fY) + fZ;
				float fPx = 0.0f, fPy = 0.0f;
				if(fSum > 1e-6f) { fPx = fX / fSum; fPy = fY / fSum; }
				float fU = (fPx + fPy) * 0.5f + 0.5f;
				float fV = (fPx - fPy) * 0.5f + 0.5f;
				pPixel[2] = (unsigned char)(cMath::Clamp(fU, 0.0f, 1.0f) * 255.0f + 0.5f);
				pPixel[1] = (unsigned char)(cMath::Clamp(fV, 0.0f, 1.0f) * 255.0f + 0.5f);
				pPixel[0] = 0;
				pPixel[3] = 255;
			}
			else if(aKind == eRemixTexture_Roughness)
			{
				// Classic mode: highlight intensity R, Blinn-Phong power
				// n = 2^(G*10) + 1 (deferred_light_frag.glsl). Power to perceptual
				// roughness with the usual (2 / (n + 2))^(1/4). A weak highlight
				// means a surface that barely reflects: toward fully rough.
				float fPower = powf(2.0f, fG * 10.0f) + 1.0f;
				float fRoughness = powf(2.0f / (fPower + 2.0f), 0.25f);
				float fStrength = cMath::Clamp(sqrtf(fR), 0.0f, 1.0f);
				fRoughness = 1.0f + (fRoughness - 1.0f) * fStrength;
				unsigned char lRoughness = (unsigned char)(cMath::Clamp(fRoughness, 0.0f, 1.0f) * 255.0f + 0.5f);
				pPixel[2] = pPixel[1] = pPixel[0] = lRoughness;
				pPixel[3] = 255;
			}
		}
	}

	//-----------------------------------------------------------------------

	bool cRemixTextureCache::WriteDds(iTexture *apTexture, const tWString &asDestPath, eRemixTexture aKind)
	{
		if(apTexture->GetType() != eTextureType_2D) return false;

		GLuint lHandle = static_cast<cSDLTexture*>(apTexture)->GetTextureHandle();
		if(lHandle == 0) return false;

		GLint lPrevTexture = 0;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &lPrevTexture);
		glBindTexture(GL_TEXTURE_2D, lHandle);

		////////////////////////////////
		// Every mip level OpenGL has, as 32-bit BGRA (OpenGL decompresses DXT).
		// Rows stay in OpenGL's order: row 0 is what texture coordinate v=0
		// samples, which is what Remix samples at v=0 in a file.
		std::vector<std::vector<unsigned char> > vLevels;
		GLint lWidth = 0, lHeight = 0;
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &lWidth);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &lHeight);
		for(int lLevel=0; lWidth > 0 && lHeight > 0; ++lLevel)
		{
			GLint lW = 0, lH = 0;
			glGetTexLevelParameteriv(GL_TEXTURE_2D, lLevel, GL_TEXTURE_WIDTH, &lW);
			glGetTexLevelParameteriv(GL_TEXTURE_2D, lLevel, GL_TEXTURE_HEIGHT, &lH);
			if(lW <= 0 || lH <= 0) break;	// no such level

			vLevels.push_back(std::vector<unsigned char>((size_t)lW * lH * 4));
			glPixelStorei(GL_PACK_ALIGNMENT, 1);
			glGetTexImage(GL_TEXTURE_2D, lLevel, GL_BGRA, GL_UNSIGNED_BYTE, &vLevels.back()[0]);
			if(lW == 1 && lH == 1) break;
		}

		glBindTexture(GL_TEXTURE_2D, (GLuint)lPrevTexture);
		while(glGetError() != GL_NO_ERROR) {}
		if(vLevels.empty()) return false;

		// Each mip level converted on its own (the engine's mips are already
		// filtered; converting them keeps them consistent)
		for(size_t i=0; i<vLevels.size(); ++i)
			ConvertPixels(&vLevels[i][0], vLevels[i].size() / 4, aKind);

		////////////////////////////////
		// Header: DDSD_CAPS|HEIGHT|WIDTH|PITCH|PIXELFORMAT|MIPMAPCOUNT,
		// DDPF_RGB|ALPHAPIXELS, DDSCAPS_TEXTURE (+COMPLEX|MIPMAP with mips)
		tDdsHeader header = {};
		header.mlSize = 124;
		header.mlFlags = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x20000;
		header.mlHeight = (unsigned int)lHeight;
		header.mlWidth = (unsigned int)lWidth;
		header.mlPitch = (unsigned int)lWidth * 4;
		header.mlMipMapCount = (unsigned int)vLevels.size();
		header.mlPfSize = 32;
		header.mlPfFlags = 0x40 | 0x1;
		header.mlPfBitCount = 32;
		header.mlPfRMask = 0x00FF0000;
		header.mlPfGMask = 0x0000FF00;
		header.mlPfBMask = 0x000000FF;
		header.mlPfAMask = 0xFF000000;
		header.mlCaps = 0x1000 | (vLevels.size() > 1 ? 0x8 | 0x400000 : 0);

		// Write to a temporary name first, so a crash never leaves half a file
		tWString sTempPath = asDestPath + L".tmp";
		FILE *pFile = _wfopen(sTempPath.c_str(), L"wb");
		if(pFile==NULL) return false;
		bool bOk = fwrite("DDS ", 4, 1, pFile) == 1 && fwrite(&header, sizeof(header), 1, pFile) == 1;
		for(size_t i=0; i<vLevels.size() && bOk; ++i)
			bOk = fwrite(&vLevels[i][0], vLevels[i].size(), 1, pFile) == 1;
		fclose(pFile);

		if(bOk==false || MoveFileExW(sTempPath.c_str(), asDestPath.c_str(), MOVEFILE_REPLACE_EXISTING)==FALSE)
		{
			DeleteFileW(sTempPath.c_str());
			return false;
		}
		return true;
	}

	//-----------------------------------------------------------------------
}
