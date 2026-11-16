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

#ifndef HPL_REMIX_CONTEXT_H
#define HPL_REMIX_CONTEXT_H

// remix_c.h includes windows.h; keep its min/max macros away from the engine
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <remix/remix_c.h>

struct IDirect3DTexture9;
struct IDirect3DQuery9;

namespace hpl {

	//-----------------------------------------

	// Owns RTX Remix for RTX mode (docs/decisions/0001 and 0002): loads the Remix
	// runtime, creates its D3D9 device with the swapchain on a hidden window, and
	// a render target shared with OpenGL. Remix renders off-screen; each frame its
	// final image is copied into the shared texture, which OpenGL draws in place
	// of the world. OpenGL stays the only one presenting to the game window.
	class cRemixContext
	{
	public:
		cRemixContext();

		// Loads Remix and sets up off-screen rendering at this size. Needs the
		// OpenGL context current (it imports the shared texture). False, with the
		// reason logged, if anything is missing.
		bool Init(int alWidth, int alHeight);
		bool IsReady(){ return mbReady; }

		// The Remix API: meshes, materials, lights, camera, instances
		remixapi_Interface* GetApi(){ return &mApi; }

		// Renders the frame Remix was given (it also presents to the hidden window)
		void Present();
		// Copies Remix's final image into the shared texture and waits for the copy
		void CopyFrame();
		// Draws the shared texture over the whole current OpenGL frame buffer
		void DrawFrame();

		////////////////////////////////
		// Remix's developer menu. It reads input from window messages through
		// Remix's hook on the hidden window, which never gets any, so the game
		// window's messages are forwarded there while the menu is open (and kept
		// from the game). The menu is drawn into Remix's frame.

		// True once after Alt+X was pressed in the game window
		bool TakeMenuToggleRequest();
		void SetMenuOpen(bool abX);
		bool IsMenuOpen(){ return mbMenuOpen; }

	private:
		static LRESULT CALLBACK GameWindowProc(HWND ahWnd, UINT alMsg, WPARAM alWParam, LPARAM alLParam);
		static LRESULT CALLBACK HiddenWindowProc(HWND ahWnd, UINT alMsg, WPARAM alWParam, LPARAM alLParam);
		void HookGameWindow();
		bool CreateDevice(int alWidth, int alHeight);
		HANDLE CreateSharedTexture(int alWidth, int alHeight);
		bool ImportIntoOpenGL(HANDLE ahShared, int alWidth, int alHeight);

		bool mbReady;
		remixapi_Interface mApi;
		HMODULE mhRemixDll;

		HWND mhHiddenWindow;
		IDirect3D9Ex *mpD3D9;
		IDirect3DDevice9Ex *mpDevice;
		IDirect3DTexture9 *mpSharedTexture;
		IDirect3DSurface9 *mpSharedSurface;
		IDirect3DQuery9 *mpCopyDoneQuery;
		unsigned int mlGlMemory;	// OpenGL memory object and texture of the shared texture
		unsigned int mlGlTexture;

		HWND mhGameWindow;
		WNDPROC mpGameWindowProc;	// the game window's own procedure (SDL's)
		WNDPROC mpHiddenWindowProc;	// Remix's hook on the hidden window
		bool mbMenuOpen;
		bool mbMenuToggleRequested;
	};

	//-----------------------------------------

};
#endif // HPL_REMIX_CONTEXT_H
