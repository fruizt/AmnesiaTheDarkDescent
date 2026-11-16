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
#include <d3d9.h>	// before remix_c.h, which only forward-declares the D3D9 types

#include "impl/RemixContext.h"

#include "system/LowLevelSystem.h"

namespace hpl {

	//////////////////////////////////////////////////////////////////////////
	// CONSTRUCTORS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	cRemixContext::cRemixContext()
	{
		mbReady = false;
		mApi = remixapi_Interface();
		mhRemixDll = NULL;

		mhHiddenWindow = NULL;
		mpD3D9 = NULL;
		mpDevice = NULL;
		mpSharedTexture = NULL;
		mpSharedSurface = NULL;
		mpCopyDoneQuery = NULL;
		mlGlMemory = 0;
		mlGlTexture = 0;

		mhGameWindow = NULL;
		mpGameWindowProc = NULL;
		mpHiddenWindowProc = NULL;
		mbMenuOpen = false;
		mbMenuToggleRequested = false;
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PUBLIC METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	bool cRemixContext::Init(int alWidth, int alHeight)
	{
		////////////////////////////////
		// Load Remix from the exe's folder (it finds its dependencies there)
		wchar_t sDllPath[MAX_PATH];
		GetModuleFileNameW(NULL, sDllPath, MAX_PATH);
		wchar_t *pFileName = wcsrchr(sDllPath, L'\\');
		if(pFileName) wcscpy_s(pFileName + 1, MAX_PATH - (pFileName + 1 - sDllPath), L"d3d9.dll");

		remixapi_ErrorCode lResult = remixapi_lib_loadRemixDllAndInitialize(sDllPath, &mApi, &mhRemixDll);
		if(lResult != REMIXAPI_ERROR_CODE_SUCCESS)
		{
			Error("Remix: could not load '%ls' (error %d)\n", sDllPath, lResult);
			return false;
		}

		if(CreateDevice(alWidth, alHeight)==false) return false;

		HANDLE hShared = CreateSharedTexture(alWidth, alHeight);
		if(hShared==NULL) return false;
		if(ImportIntoOpenGL(hShared, alWidth, alHeight)==false) return false;

		// Start with Remix's UI closed (no first-use guide)
		mApi.SetConfigVariable("rtx.showUI", "0");
		HookGameWindow();

		mbReady = true;
		return true;
	}

	//-----------------------------------------------------------------------

	bool cRemixContext::TakeMenuToggleRequest()
	{
		bool bRequested = mbMenuToggleRequested;
		mbMenuToggleRequested = false;
		return bRequested;
	}

	//-----------------------------------------------------------------------

	void cRemixContext::SetMenuOpen(bool abX)
	{
		if(mbMenuOpen == abX) return;
		mbMenuOpen = abX;
		mApi.SetConfigVariable("rtx.showUI", abX ? "2" : "0");	// 2 = advanced menu
		Log("Remix: developer menu %s\n", abX ? "opened" : "closed");
	}

	//-----------------------------------------------------------------------

	void cRemixContext::Present()
	{
		remixapi_PresentInfo presentInfo = {};
		presentInfo.sType = REMIXAPI_STRUCT_TYPE_PRESENT_INFO;
		mApi.Present(&presentInfo);
	}

	//-----------------------------------------------------------------------

	void cRemixContext::CopyFrame()
	{
		// The simple, conservative sync for now: the CPU waits until the copy is done
		mApi.dxvk_CopyRenderingOutput(mpSharedSurface, REMIXAPI_DXVK_COPY_RENDERING_OUTPUT_TYPE_FINAL_COLOR);

		// The developer menu, drawn over the frame in the same texture (needs
		// rtx.gui.enableExternalPresenter, see remix/rtx.conf). This is also where
		// Remix's menu runs its frame and reads the input forwarded to it.
		if(mbMenuOpen)
			mApi.dxvk_CopyRenderingOutput(mpSharedSurface, REMIXAPI_DXVK_COPY_RENDERING_OUTPUT_TYPE_GUI);

		if(mpCopyDoneQuery)
		{
			mpCopyDoneQuery->Issue(D3DISSUE_END);
			while(mpCopyDoneQuery->GetData(NULL, 0, D3DGETDATA_FLUSH) == S_FALSE) Sleep(0);
		}
	}

	//-----------------------------------------------------------------------

	// Plain OpenGL with every touched state saved and restored, since the engine
	// caches its GL state
	void cRemixContext::DrawFrame()
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
		glBindTexture(GL_TEXTURE_2D, mlGlTexture);
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

	//////////////////////////////////////////////////////////////////////////
	// PRIVATE METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	// The context whose menu the game window's messages go to (there's one)
	static cRemixContext *gpMenuContext = NULL;

	static BOOL CALLBACK FindGameWindowProc(HWND ahWnd, LPARAM alParam)
	{
		wchar_t sClass[32] = {};
		GetClassNameW(ahWnd, sClass, 32);
		if(wcscmp(sClass, L"SDL_app") != 0) return TRUE;
		*reinterpret_cast<HWND*>(alParam) = ahWnd;
		return FALSE;
	}

	static bool IsMenuInputMessage(UINT alMsg)
	{
		return	(alMsg >= WM_MOUSEFIRST && alMsg <= WM_MOUSELAST) ||	// moves, buttons, wheels
				(alMsg >= WM_KEYFIRST && alMsg <= WM_KEYLAST) ||		// keys, system keys, text
				alMsg == WM_INPUT;										// raw input (SDL's mouse look)
	}

	//-----------------------------------------------------------------------

	// Subclass the game window (the engine's SDL window, on this thread), as
	// Remix does with the windows it presents to
	void cRemixContext::HookGameWindow()
	{
		EnumThreadWindows(GetCurrentThreadId(), FindGameWindowProc, reinterpret_cast<LPARAM>(&mhGameWindow));
		if(mhGameWindow==NULL)
		{
			Warning("Remix: game window not found, the developer menu (Alt+X) won't get input\n");
			return;
		}
		gpMenuContext = this;
		mpGameWindowProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(mhGameWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(GameWindowProc)));

		// In front of Remix's hook on the hidden window
		mpHiddenWindowProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(mhHiddenWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HiddenWindowProc)));
	}

	//-----------------------------------------------------------------------

	LRESULT CALLBACK cRemixContext::HiddenWindowProc(HWND ahWnd, UINT alMsg, WPARAM alWParam, LPARAM alLParam)
	{
		// The menu asks Windows to report when the mouse leaves the hidden
		// window, and the mouse is never over it: Windows answers at once, and
		// the menu forgets the mouse position (no cursor, no clicks)
		if(alMsg == WM_MOUSELEAVE || alMsg == WM_NCMOUSELEAVE) return 0;

		return CallWindowProcW(gpMenuContext->mpHiddenWindowProc, ahWnd, alMsg, alWParam, alLParam);
	}

	//-----------------------------------------------------------------------

	LRESULT CALLBACK cRemixContext::GameWindowProc(HWND ahWnd, UINT alMsg, WPARAM alWParam, LPARAM alLParam)
	{
		cRemixContext *pContext = gpMenuContext;

		// Alt+X (a system key, since Alt is held): ask to toggle the menu. Not
		// passed on, so neither the game nor Remix's own hotkey sees it.
		if(alMsg == WM_SYSKEYDOWN && alWParam == 'X')
		{
			if((alLParam & (1 << 30)) == 0) pContext->mbMenuToggleRequested = true;	// not a key repeat
			return 0;
		}

		if(pContext->mbMenuOpen && IsMenuInputMessage(alMsg))
		{
			// To the menu (through Remix's hook on its window; same client size,
			// so mouse coordinates match). Raw input is the game's mouse look.
			if(alMsg != WM_INPUT) SendMessageW(pContext->mhHiddenWindow, alMsg, alWParam, alLParam);

			// On a click the menu captures the mouse to the hidden window, which
			// sits elsewhere on screen: drags would get its coordinates. Keep the
			// capture on the game window, whose messages come here.
			bool bButtonDown =	alMsg == WM_LBUTTONDOWN || alMsg == WM_RBUTTONDOWN || alMsg == WM_MBUTTONDOWN || alMsg == WM_XBUTTONDOWN ||
								alMsg == WM_LBUTTONDBLCLK || alMsg == WM_RBUTTONDBLCLK || alMsg == WM_MBUTTONDBLCLK || alMsg == WM_XBUTTONDBLCLK;
			bool bButtonUp = alMsg == WM_LBUTTONUP || alMsg == WM_RBUTTONUP || alMsg == WM_MBUTTONUP || alMsg == WM_XBUTTONUP;
			if(bButtonDown && GetCapture() == pContext->mhHiddenWindow) SetCapture(ahWnd);
			if(bButtonUp && GetCapture() == ahWnd && (alWParam & (MK_LBUTTON|MK_RBUTTON|MK_MBUTTON|MK_XBUTTON1|MK_XBUTTON2)) == 0) ReleaseCapture();

			// Key releases still reach the game, so no key stays held there
			bool bKeyUp = alMsg == WM_KEYUP || alMsg == WM_SYSKEYUP;
			if(bKeyUp==false) return 0;
		}

		return CallWindowProcW(pContext->mpGameWindowProc, ahWnd, alMsg, alWParam, alLParam);
	}

	//-----------------------------------------------------------------------

	// What remixapi_Startup does, but keeping the device: we need it to create the
	// shared texture. Its swapchain goes to a hidden window of the given size, so
	// the game window stays OpenGL's.
	bool cRemixContext::CreateDevice(int alWidth, int alHeight)
	{
		WNDCLASSW windowClass = {};
		windowClass.lpfnWndProc = DefWindowProcW;
		windowClass.hInstance = GetModuleHandleW(NULL);
		windowClass.lpszClassName = L"HPL2_RemixHidden";
		RegisterClassW(&windowClass);

		// Never shown. Remix sizes its output from this window's client area.
		RECT rect = { 0, 0, alWidth, alHeight };
		AdjustWindowRect(&rect, WS_POPUP, FALSE);
		mhHiddenWindow = CreateWindowExW(0, windowClass.lpszClassName, L"Remix", WS_POPUP, 0, 0,
										rect.right - rect.left, rect.bottom - rect.top,
										NULL, NULL, windowClass.hInstance, NULL);
		if(mhHiddenWindow==NULL)
		{
			Error("Remix: could not create the hidden window\n");
			return false;
		}

		remixapi_ErrorCode lResult = mApi.dxvk_CreateD3D9(false, &mpD3D9);
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
		presentParams.hDeviceWindow = mhHiddenWindow;
		presentParams.Windowed = TRUE;

		HRESULT hr = mpD3D9->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, mhHiddenWindow,
											D3DCREATE_HARDWARE_VERTEXPROCESSING, &presentParams, NULL, &mpDevice);
		if(FAILED(hr) || mpDevice==NULL)
		{
			Error("Remix: CreateDeviceEx failed (0x%08x)\n", (unsigned)hr);
			return false;
		}

		lResult = mApi.dxvk_RegisterD3D9Device(mpDevice);
		if(lResult != REMIXAPI_ERROR_CODE_SUCCESS)
		{
			Error("Remix: dxvk_RegisterD3D9Device failed (error %d)\n", lResult);
			return false;
		}
		return true;
	}

	//-----------------------------------------------------------------------

	// A render target on Remix's device whose memory Windows can share (Vulkan
	// memory exported as an OPAQUE_WIN32_KMT handle)
	HANDLE cRemixContext::CreateSharedTexture(int alWidth, int alHeight)
	{
		// RGBA byte order, which OpenGL's GL_RGBA8 also uses (the copy converts
		// from Remix's BGRA back buffer)
		HANDLE hShared = NULL;
		HRESULT hr = mpDevice->CreateTexture((UINT)alWidth, (UINT)alHeight, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8B8G8R8,
											D3DPOOL_DEFAULT, &mpSharedTexture, &hShared);
		if(FAILED(hr) || mpSharedTexture==NULL || hShared==NULL || hShared==INVALID_HANDLE_VALUE)
		{
			Error("Remix: could not create the shared texture (0x%08x)\n", (unsigned)hr);
			return NULL;
		}
		mpSharedTexture->GetSurfaceLevel(0, &mpSharedSurface);

		// Lets the CPU wait until Remix's copy into it is done
		mpDevice->CreateQuery(D3DQUERYTYPE_EVENT, &mpCopyDoneQuery);
		return hShared;
	}

	//-----------------------------------------------------------------------

	// Make the shared memory an OpenGL texture (GL_EXT_memory_object_win32).
	// Remix allocates it as a dedicated allocation of a size it doesn't report, and
	// the GPU pads images for its tiling. The driver refuses a size too small
	// (GL_INVALID_VALUE) and one larger than the allocation (GL_OUT_OF_MEMORY), so
	// search upwards from the image's byte size in 64 KB steps.
	bool cRemixContext::ImportIntoOpenGL(HANDLE ahShared, int alWidth, int alHeight)
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
		for(GLuint64 lSize = lMinSize; lSize <= lMinSize * 2 && mlGlTexture==0; lSize += lStep)
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
				mlGlMemory = lMemory;
				mlGlTexture = lTexture;
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
		if(mlGlTexture==0) Error("Remix: could not import the shared texture into OpenGL\n");
		return mlGlTexture != 0;
	}

	//-----------------------------------------------------------------------
}
