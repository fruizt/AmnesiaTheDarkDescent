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
#include <GL/glew.h>
#include <d3d9.h>	// before remix_c.h, which only forward-declares the D3D9 types
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
	static HWND ghGameWindow = NULL;	// the engine's SDL window, only OpenGL presents to it

	// Remix renders off-screen. Its D3D9 device presents to a hidden window, and
	// each frame its final image is copied into a texture shared with OpenGL
	// (docs/MODERNIZATION.md, "RTX mode design").
	static HWND ghHiddenWindow = NULL;
	static IDirect3D9Ex *gpD3D9 = NULL;
	static IDirect3DDevice9Ex *gpDevice = NULL;
	static IDirect3DTexture9 *gpSharedTexture = NULL;
	static IDirect3DSurface9 *gpSharedSurface = NULL;
	static IDirect3DQuery9 *gpCopyDoneQuery = NULL;
	static GLuint glGlMemory = 0;
	static GLuint glGlTexture = 0;	// 0 if the OpenGL import failed

	static bool gbStartupTried = false;
	static bool gbRunning = false;
	static int glLastFrame = -1;

	// RTX mode: Remix's image is drawn in place of the OpenGL world render.
	// Classic mode: OpenGL as usual, Remix idle. Toggled with F9.
	static bool gbRtxActive = false;
	static bool gbToggleKeyWasDown = false;

	static float gfLightScale = 100.0f;
	static bool gbFlipV = false;
	static bool gbNormalMaps = false;

	// Remix resources, created once and reused
	typedef std::pair<iVertexBuffer*, cMaterial*> tMeshKey;
	static std::map<cMaterial*, remixapi_MaterialHandle> gmapMaterials;
	static std::map<tMeshKey, remixapi_MeshHandle> gmapMeshes;

	// The caches are keyed by engine pointers, which a new world can reuse once the
	// old one is destroyed (that gave Rainy Hall objects the menu's materials).
	// Caches are cleared when a world is destroyed (OnWorldDestroyed). As a safety
	// net, each entry also remembers what it was made from; a hit that doesn't
	// match is rebuilt instead of reused, and counted.
	static std::map<tMeshKey, std::pair<int,int> > gmapMeshCounts;	// vertices, indices
	static std::map<cMaterial*, tWString> gmapMaterialAlbedo;
	static int glStaleMeshHits = 0, glStaleMaterialHits = 0;
	static int glLoggedStaleMeshHits = -1, glLoggedStaleMaterialHits = -1;
	// Lights are recreated every frame (as the Remix docs say to do for changing lights)
	static std::vector<remixapi_LightHandle> gvFrameLights;

	// Frame timings (CPU wall clock, in ms), averaged and logged every 5 seconds.
	// GPU work is asynchronous, so a GPU-bound step shows up where the CPU waits
	// for it: in copy+wait for Remix, in the frame total for OpenGL.
	enum ePerfStep
	{
		ePerfStep_Frame,		// start of one world render to the start of the next
		ePerfStep_GlWorld,		// OpenGL world render (CPU side)
		ePerfStep_RemixScene,	// camera, objects and lights handed to Remix
		ePerfStep_RemixPresent,	// Remix's Present call
		ePerfStep_CopyWait,		// copy into the shared texture, and waiting for it
		ePerfStep_Draw,			// OpenGL drawing the shared texture
		ePerfStep_LastEnum
	};
	static const char *gvPerfStepNames[ePerfStep_LastEnum] = { "frame", "GL world", "Remix scene", "Remix present", "copy+wait", "draw" };

	static double gfTimerToMs = 0.0;
	static double gvPerfSum[ePerfStep_LastEnum] = {};
	static double gfPerfMaxFrame = 0.0;
	static int glPerfFrames = 0;
	static double gfPerfWindowStart = -1.0;
	static double gfFrameStart = -1.0;
	static double gfPendingFrameMs = -1.0;	// the previous frame's total, recorded with this frame's steps
	static int glLastBeforeRenderFrame = -1;

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

	static double NowMs()
	{
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		if(gfTimerToMs == 0.0)
		{
			LARGE_INTEGER freq;
			QueryPerformanceFrequency(&freq);
			gfTimerToMs = 1000.0 / (double)freq.QuadPart;
		}
		return (double)now.QuadPart * gfTimerToMs;
	}

	static void ResetPerf()
	{
		for(int i=0; i<ePerfStep_LastEnum; ++i) gvPerfSum[i] = 0.0;
		gfPerfMaxFrame = 0.0;
		glPerfFrames = 0;
		gfPerfWindowStart = NowMs();
	}

	// One frame's step times. The frame total is the previous frame's (it's only
	// known when the next one starts), close enough for 5-second averages.
	static void RecordPerf(const double *apStepMs)
	{
		if(gfPendingFrameMs < 0.0) return;	// no complete frame yet
		if(gfPerfWindowStart < 0.0) ResetPerf();

		gvPerfSum[ePerfStep_Frame] += gfPendingFrameMs;
		if(gfPendingFrameMs > gfPerfMaxFrame) gfPerfMaxFrame = gfPendingFrameMs;
		for(int i=ePerfStep_Frame+1; i<ePerfStep_LastEnum; ++i) gvPerfSum[i] += apStepMs[i];
		++glPerfFrames;

		if(NowMs() - gfPerfWindowStart < 5000.0) return;

		// "Remix perf [RTX]: 143.2 fps | frame 6.98 avg, 12.40 max | GL world 2.10 | ..."
		char sLine[512];
		double fFrameAvg = gvPerfSum[ePerfStep_Frame] / glPerfFrames;
		int lLen = sprintf_s(sLine, "Remix perf [%s]: %.1f fps | frame %.2f avg, %.2f max",
							gbRtxActive ? "RTX" : "classic", 1000.0 / fFrameAvg, fFrameAvg, gfPerfMaxFrame);
		int lLastStep = gbRtxActive ? ePerfStep_LastEnum : ePerfStep_GlWorld + 1;
		for(int i=ePerfStep_GlWorld; i<lLastStep && lLen > 0; ++i)
			lLen += sprintf_s(sLine + lLen, sizeof(sLine) - lLen, " | %s %.2f", gvPerfStepNames[i], gvPerfSum[i] / glPerfFrames);
		Log("%s (ms, %d frames)\n", sLine, glPerfFrames);

		ResetPerf();
	}

	//-----------------------------------------------------------------------

	static BOOL CALLBACK FindGameWindowProc(HWND ahWnd, LPARAM)
	{
		wchar_t sClass[32] = {};
		GetClassNameW(ahWnd, sClass, 32);
		if(wcscmp(sClass, L"SDL_app") == 0 && IsWindowVisible(ahWnd))
		{
			ghGameWindow = ahWnd;
			return FALSE;
		}
		return TRUE;
	}

	//-----------------------------------------------------------------------

	// What remixapi_Startup does, but keeping the device: we need it to create the
	// shared texture. Its swapchain goes to a hidden window of the game window's
	// size, so the game window stays OpenGL's.
	static bool CreateRemixDevice(int alWidth, int alHeight)
	{
		WNDCLASSW windowClass = {};
		windowClass.lpfnWndProc = DefWindowProcW;
		windowClass.hInstance = GetModuleHandleW(NULL);
		windowClass.lpszClassName = L"HPL2_RemixHidden";
		RegisterClassW(&windowClass);

		// Never shown. Remix sizes its output from this window's client area.
		RECT rect = { 0, 0, alWidth, alHeight };
		AdjustWindowRect(&rect, WS_POPUP, FALSE);
		ghHiddenWindow = CreateWindowExW(0, windowClass.lpszClassName, L"Remix", WS_POPUP, 0, 0,
										rect.right - rect.left, rect.bottom - rect.top,
										NULL, NULL, windowClass.hInstance, NULL);
		if(ghHiddenWindow==NULL)
		{
			Error("Remix: could not create the hidden window\n");
			return false;
		}

		remixapi_ErrorCode lResult = gRemix.dxvk_CreateD3D9(false, &gpD3D9);
		if(lResult != REMIXAPI_ERROR_CODE_SUCCESS)
		{
			Error("Remix: dxvk_CreateD3D9 failed (error %d)\n", lResult);
			return false;
		}

		// The same parameters as remixapi_Startup
		D3DPRESENT_PARAMETERS presentParams = {};
		presentParams.BackBufferWidth = (UINT)alWidth;
		presentParams.BackBufferHeight = (UINT)alHeight;
		presentParams.BackBufferFormat = D3DFMT_UNKNOWN;
		presentParams.SwapEffect = D3DSWAPEFFECT_DISCARD;
		presentParams.hDeviceWindow = ghHiddenWindow;
		presentParams.Windowed = TRUE;

		HRESULT hr = gpD3D9->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, ghHiddenWindow,
											D3DCREATE_HARDWARE_VERTEXPROCESSING, &presentParams, NULL, &gpDevice);
		if(FAILED(hr) || gpDevice==NULL)
		{
			Error("Remix: CreateDeviceEx failed (0x%08x)\n", (unsigned)hr);
			return false;
		}

		lResult = gRemix.dxvk_RegisterD3D9Device(gpDevice);
		if(lResult != REMIXAPI_ERROR_CODE_SUCCESS)
		{
			Error("Remix: dxvk_RegisterD3D9Device failed (error %d)\n", lResult);
			return false;
		}
		return true;
	}

	//-----------------------------------------------------------------------

	// A render target on Remix's device whose memory Windows can share
	// (Vulkan memory exported as an OPAQUE_WIN32_KMT handle)
	static HANDLE CreateSharedTexture(int alWidth, int alHeight)
	{
		// RGBA byte order, which OpenGL's GL_RGBA8 also uses (the copy converts
		// from Remix's BGRA back buffer)
		HANDLE hShared = NULL;
		HRESULT hr = gpDevice->CreateTexture((UINT)alWidth, (UINT)alHeight, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8B8G8R8,
											D3DPOOL_DEFAULT, &gpSharedTexture, &hShared);
		if(FAILED(hr) || gpSharedTexture==NULL || hShared==NULL || hShared==INVALID_HANDLE_VALUE)
		{
			Error("Remix: could not create the shared texture (0x%08x)\n", (unsigned)hr);
			return NULL;
		}
		gpSharedTexture->GetSurfaceLevel(0, &gpSharedSurface);

		// Lets the CPU wait until Remix's copy into it is done
		gpDevice->CreateQuery(D3DQUERYTYPE_EVENT, &gpCopyDoneQuery);
		return hShared;
	}

	//-----------------------------------------------------------------------

	// Make the shared memory an OpenGL texture (GL_EXT_memory_object_win32).
	// Remix allocates it as a dedicated allocation of a size it doesn't report, and
	// the GPU pads images for its tiling. The driver refuses a size too small
	// (GL_INVALID_VALUE) and one larger than the allocation (GL_OUT_OF_MEMORY), so
	// search upwards from the image's byte size in 64 KB steps.
	static bool ImportIntoOpenGL(HANDLE ahShared, int alWidth, int alHeight)
	{
		if(GLEW_EXT_memory_object==false || GLEW_EXT_memory_object_win32==false)
		{
			Error("Remix: OpenGL lacks GL_EXT_memory_object(_win32), can't show Remix's image\n");
			return false;
		}

		GLint lPrevTexture = 0;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &lPrevTexture);
		while(glGetError() != GL_NO_ERROR) {}	// don't blame earlier errors on the import

		const GLuint64 lStep = 0x10000;	// 64 KB
		GLuint64 lMinSize = ((GLuint64)alWidth * alHeight * 4 + lStep - 1) & ~(lStep - 1);
		GLenum lLastError = GL_NO_ERROR;
		for(GLuint64 lSize = lMinSize; lSize <= lMinSize * 2 && glGlTexture==0; lSize += lStep)
		{
			GLuint lMemory = 0, lTexture = 0;
			glCreateMemoryObjectsEXT(1, &lMemory);
			GLint lDedicated = GL_TRUE;
			glMemoryObjectParameterivEXT(lMemory, GL_DEDICATED_MEMORY_OBJECT_EXT, &lDedicated);
			glImportMemoryWin32HandleEXT(lMemory, lSize, GL_HANDLE_TYPE_OPAQUE_WIN32_KMT_EXT, ahShared);

			glGenTextures(1, &lTexture);
			glBindTexture(GL_TEXTURE_2D, lTexture);
			glTexStorageMem2DEXT(GL_TEXTURE_2D, 1, GL_RGBA8, alWidth, alHeight, lMemory, 0);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

			GLenum lError = glGetError();
			while(glGetError() != GL_NO_ERROR) {}
			if(lError == GL_NO_ERROR)
			{
				glGlMemory = lMemory;
				glGlTexture = lTexture;
				Log("Remix: shared texture imported into OpenGL (%dx%d, %llu bytes)\n", alWidth, alHeight, (unsigned long long)lSize);
			}
			else
			{
				// Log only when the kind of failure changes, not every step
				if(lError != lLastError)
					Log("Remix: OpenGL import with %llu bytes failed (GL error 0x%04x)\n", (unsigned long long)lSize, lError);
				lLastError = lError;
				glDeleteTextures(1, &lTexture);
				glDeleteMemoryObjectsEXT(1, &lMemory);
			}
		}

		glBindTexture(GL_TEXTURE_2D, (GLuint)lPrevTexture);
		if(glGlTexture==0) Error("Remix: could not import the shared texture into OpenGL\n");
		return glGlTexture != 0;
	}

	//-----------------------------------------------------------------------

	// Draw Remix's image over the whole current frame buffer, in place of the
	// world. Plain OpenGL with every touched state saved and restored, since the
	// engine caches its GL state.
	static void DrawSharedTexture()
	{
		GLint lPrevProgram = 0, lPrevActiveTexture = 0, lPrevTexture = 0;
		glGetIntegerv(GL_CURRENT_PROGRAM, &lPrevProgram);
		glGetIntegerv(GL_ACTIVE_TEXTURE, &lPrevActiveTexture);
		glActiveTexture(GL_TEXTURE0);
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &lPrevTexture);
		glPushAttrib(GL_ALL_ATTRIB_BITS);

		glUseProgram(0);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_STENCIL_TEST);
		glDisable(GL_BLEND);
		glDisable(GL_ALPHA_TEST);
		glDisable(GL_CULL_FACE);
		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_LIGHTING);
		glDisable(GL_FOG);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, glGlTexture);
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

		glMatrixMode(GL_TEXTURE);		glPushMatrix(); glLoadIdentity();
		glMatrixMode(GL_PROJECTION);	glPushMatrix(); glLoadIdentity();
		glMatrixMode(GL_MODELVIEW);		glPushMatrix(); glLoadIdentity();

		// Remix's first row is the top of the image; OpenGL's t=0 is that row
		glBegin(GL_QUADS);
		glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f, -1.0f);
		glTexCoord2f(1.0f, 1.0f); glVertex2f( 1.0f, -1.0f);
		glTexCoord2f(1.0f, 0.0f); glVertex2f( 1.0f,  1.0f);
		glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f,  1.0f);
		glEnd();

		glMatrixMode(GL_TEXTURE);		glPopMatrix();
		glMatrixMode(GL_PROJECTION);	glPopMatrix();
		glMatrixMode(GL_MODELVIEW);		glPopMatrix();

		glPopAttrib();
		glBindTexture(GL_TEXTURE_2D, (GLuint)lPrevTexture);
		glActiveTexture((GLenum)lPrevActiveTexture);
		glUseProgram((GLuint)lPrevProgram);
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
		// The game's window (the engine's SDL window, created on this thread):
		// Remix renders at its size, and F9 only counts while it has focus
		EnumThreadWindows(GetCurrentThreadId(), FindGameWindowProc, 0);
		if(ghGameWindow==NULL)
		{
			Error("Remix: could not find the game window\n");
			return false;
		}
		RECT clientRect = {};
		GetClientRect(ghGameWindow, &clientRect);
		int lWidth = clientRect.right - clientRect.left;
		int lHeight = clientRect.bottom - clientRect.top;
		gbRtxActive = EnvFlag("HPL_REMIX_START_RTX");

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

		if(CreateRemixDevice(lWidth, lHeight)==false) return false;

		HANDLE hShared = CreateSharedTexture(lWidth, lHeight);
		if(hShared==NULL) return false;
		if(ImportIntoOpenGL(hShared, lWidth, lHeight)==false) return false;

		// Start with Remix's UI closed (no first-use guide)
		gRemix.SetConfigVariable("rtx.showUI", "0");

		Log("Remix: started off-screen at %dx%d, %s mode, F9 toggles (light scale %.1f, flip V %d, normal maps %d)\n",
			lWidth, lHeight, gbRtxActive ? "RTX" : "classic", gfLightScale, gbFlipV, gbNormalMaps);
		return true;
	}

	//-----------------------------------------------------------------------

	// Drop a material's Remix resources, and every cached mesh that uses it
	static void InvalidateMaterial(cMaterial *apMaterial)
	{
		std::map<tMeshKey, remixapi_MeshHandle>::iterator meshIt = gmapMeshes.begin();
		while(meshIt != gmapMeshes.end())
		{
			if(meshIt->first.second == apMaterial)
			{
				if(meshIt->second) gRemix.DestroyMesh(meshIt->second);
				gmapMeshCounts.erase(meshIt->first);
				meshIt = gmapMeshes.erase(meshIt);
			}
			else ++meshIt;
		}

		std::map<cMaterial*, remixapi_MaterialHandle>::iterator matIt = gmapMaterials.find(apMaterial);
		if(matIt != gmapMaterials.end())
		{
			if(matIt->second) gRemix.DestroyMaterial(matIt->second);
			gmapMaterials.erase(matIt);
		}
		gmapMaterialAlbedo.erase(apMaterial);
	}

	//-----------------------------------------------------------------------

	// Drop everything (meshes first, they reference materials)
	static void ClearCaches()
	{
		for(std::map<tMeshKey, remixapi_MeshHandle>::iterator it = gmapMeshes.begin(); it != gmapMeshes.end(); ++it)
			if(it->second) gRemix.DestroyMesh(it->second);
		for(std::map<cMaterial*, remixapi_MaterialHandle>::iterator it = gmapMaterials.begin(); it != gmapMaterials.end(); ++it)
			if(it->second) gRemix.DestroyMaterial(it->second);

		Log("Remix: world destroyed, dropped %d cached meshes and %d materials\n", (int)gmapMeshes.size(), (int)gmapMaterials.size());
		gmapMeshes.clear();
		gmapMeshCounts.clear();
		gmapMaterials.clear();
		gmapMaterialAlbedo.clear();
	}

	//-----------------------------------------------------------------------

	static remixapi_MaterialHandle GetRemixMaterial(cMaterial *apMaterial)
	{
		if(apMaterial==NULL) return NULL;

		std::map<cMaterial*, remixapi_MaterialHandle>::iterator it = gmapMaterials.find(apMaterial);
		if(it != gmapMaterials.end())
		{
			// Safety net: reuse only if the entry was made from this material
			iTexture *pDiffuse = apMaterial->GetTexture(eMaterialTexture_Diffuse);
			tWString sNow = pDiffuse ? pDiffuse->GetFullPath() : tWString();
			if(sNow == gmapMaterialAlbedo[apMaterial]) return it->second;

			++glStaleMaterialHits;
			InvalidateMaterial(apMaterial);	// then rebuild below
		}

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
		gmapMaterialAlbedo[apMaterial] = sAlbedo;
		return hMaterial;
	}

	//-----------------------------------------------------------------------

	static remixapi_MeshHandle GetRemixMesh(iVertexBuffer *apVtxBuffer, cMaterial *apMaterial)
	{
		// Material first: if its cache entry was stale, this also drops the meshes
		// that used it, so the lookup below can't return one of those
		remixapi_MaterialHandle hMaterial = GetRemixMaterial(apMaterial);

		tMeshKey key(apVtxBuffer, apMaterial);
		std::map<tMeshKey, remixapi_MeshHandle>::iterator it = gmapMeshes.find(key);
		if(it != gmapMeshes.end())
		{
			// Safety net: reuse only if the entry was made from a buffer of this size
			const std::pair<int,int> &cached = gmapMeshCounts[key];
			if(cached.first == apVtxBuffer->GetVertexNum() && cached.second == apVtxBuffer->GetIndexNum())
				return it->second;

			++glStaleMeshHits;
			if(it->second) gRemix.DestroyMesh(it->second);	// then rebuild below
			gmapMeshes.erase(it);
		}

		remixapi_MeshHandle hMesh = NULL;
		gmapMeshes[key] = NULL;	// also remembers failures, so they're tried once
		gmapMeshCounts[key] = std::pair<int,int>(apVtxBuffer->GetVertexNum(), apVtxBuffer->GetIndexNum());

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
		surface.material = hMaterial;

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

	// Billboards that fake lighting effects which path tracing produces for real
	// (Remix's volumetric lighting makes actual light shafts). Matched against
	// the material name. They stay in the OpenGL renderer (classic mode).
	static const char *gvSkippedBillboardMaterials[] = { "lightray" };

	static bool IsSkippedBillboard(iRenderable *apObject)
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

	static void DrawRenderable(iRenderable *apObject, cFrustum *apFrustum)
	{
		if(IsSkippedBillboard(apObject)) return;

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

		double vStepMs[ePerfStep_LastEnum] = {};
		double fStepStart = NowMs();
		vStepMs[ePerfStep_GlWorld] = gfFrameStart >= 0.0 ? fStepStart - gfFrameStart : 0.0;

		// F9 toggles classic / RTX (only while the game window has focus)
		bool bToggleKeyDown = GetForegroundWindow() == ghGameWindow && (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
		if(bToggleKeyDown && gbToggleKeyWasDown==false)
		{
			gbRtxActive = !gbRtxActive;
			Log("Remix: switched to %s mode\n", gbRtxActive ? "RTX" : "classic");
			ResetPerf();	// don't mix the two modes in one average
			gfPendingFrameMs = -1.0;
		}
		gbToggleKeyWasDown = bToggleKeyDown;

		if(gbRtxActive==false)	// classic: OpenGL as usual, Remix stays idle
		{
			RecordPerf(vStepMs);
			return;
		}

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

		// Cache totals, logged about every 5 seconds when the stale counts change
		if(lFrame % 300 == 0 && (glStaleMeshHits != glLoggedStaleMeshHits || glStaleMaterialHits != glLoggedStaleMaterialHits))
		{
			Log("Remix: frame %d: %d meshes / %d materials cached, stale entries rebuilt so far: %d mesh, %d material\n",
				lFrame, (int)gmapMeshes.size(), (int)gmapMaterials.size(), glStaleMeshHits, glStaleMaterialHits);
			glLoggedStaleMeshHits = glStaleMeshHits;
			glLoggedStaleMaterialHits = glStaleMaterialHits;
		}

		double fNow = NowMs();
		vStepMs[ePerfStep_RemixScene] = fNow - fStepStart;
		fStepStart = fNow;

		remixapi_PresentInfo presentInfo = {};
		presentInfo.sType = REMIXAPI_STRUCT_TYPE_PRESENT_INFO;
		gRemix.Present(&presentInfo);	// renders, and presents to the hidden window

		fNow = NowMs();
		vStepMs[ePerfStep_RemixPresent] = fNow - fStepStart;
		fStepStart = fNow;

		////////////////////////////////
		// Copy Remix's final image into the shared texture, wait until the copy
		// is done (the simple, conservative sync for now), then draw it in place
		// of the world. Post effects and the GUI then draw on top as usual.
		gRemix.dxvk_CopyRenderingOutput(gpSharedSurface, REMIXAPI_DXVK_COPY_RENDERING_OUTPUT_TYPE_FINAL_COLOR);
		if(gpCopyDoneQuery)
		{
			gpCopyDoneQuery->Issue(D3DISSUE_END);
			while(gpCopyDoneQuery->GetData(NULL, 0, D3DGETDATA_FLUSH) == S_FALSE) Sleep(0);
		}

		fNow = NowMs();
		vStepMs[ePerfStep_CopyWait] = fNow - fStepStart;
		fStepStart = fNow;

		DrawSharedTexture();

		vStepMs[ePerfStep_Draw] = NowMs() - fStepStart;
		RecordPerf(vStepMs);
	}

	//-----------------------------------------------------------------------

	void cRemixSpike::OnBeforeWorldRender()
	{
		if(gbRunning==false) return;

		// Once per frame: the first viewport rendered is the game's main one
		int lFrame = iRenderer::GetRenderFrameCount();
		if(lFrame == glLastBeforeRenderFrame) return;
		glLastBeforeRenderFrame = lFrame;

		double fNow = NowMs();
		if(gfFrameStart >= 0.0) gfPendingFrameMs = fNow - gfFrameStart;
		gfFrameStart = fNow;
	}

	//-----------------------------------------------------------------------

	void cRemixSpike::OnWorldDestroyed()
	{
		if(gbRunning) ClearCaches();
	}

	//-----------------------------------------------------------------------
}
