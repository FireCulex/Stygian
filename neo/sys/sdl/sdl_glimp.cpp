/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

===========================================================================
*/

#include "sdl_local.h"
#include <SDL.h>
#include <SDL_opengl.h>
#include "../../renderer/tr_local.h"	// glimpParms_t, glConfig, GLimp_* declarations

extern idCVar r_windowWidth;	// defined in RenderSystem_init.cpp
extern idCVar r_windowHeight;

GLExtension_t GLimp_ExtensionPointer( const char *name ) {
	return (GLExtension_t)SDL_GL_GetProcAddress( name );
}

// The renderer sizes its viewport from glConfig.nativeScreenWidth/Height
// (idRenderSystemLocal::GetWidth/GetHeight). If those stay 0 the render crop
// becomes {0,0,-1,-1} and GL_Scissor(0,0,0,0) clips away every draw -> black
// screen. The win32 GLimp sets these in GLimp_Init/GLimp_SetScreenParms
// (win_glimp.cpp); the SDL port must do the same, using the real GL drawable
// size so windowed resizes stay consistent.
static void GLimp_UpdateScreenParms( glimpParms_t parms ) {
	int drawableW = parms.width;
	int drawableH = parms.height;
	if ( sysLocalSDL.window != nullptr ) {
		SDL_GL_GetDrawableSize( sysLocalSDL.window, &drawableW, &drawableH );
	}

	glConfig.isFullscreen = parms.fullScreen;
	glConfig.isStereoPixelFormat = parms.stereo;
	glConfig.nativeScreenWidth = drawableW;
	glConfig.nativeScreenHeight = drawableH;
	glConfig.multisamples = parms.multiSamples;
	glConfig.pixelAspect = 1.0f;	// FIXME: some monitor modes may be distorted

	// physical screen width in centimeters (best effort via DPI, like the win32 fallback)
	float ddpi = 0.0f, hdpi = 0.0f, vdpi = 0.0f;
	SDL_DisplayMode mode;
	if ( SDL_GetCurrentDisplayMode( 0, &mode ) == 0 &&
		 SDL_GetDisplayDPI( 0, &ddpi, &hdpi, &vdpi ) == 0 && hdpi > 0.0f ) {
		glConfig.physicalScreenWidthInCentimeters = ( mode.w / hdpi ) * 2.54f;
	} else {
		glConfig.physicalScreenWidthInCentimeters = 100.0f;
	}
}

// Called from Sys_GenerateEvents on SDL_WINDOWEVENT_RESIZED/SIZE_CHANGED.
// Mirrors the win32 WM_WINDOWPOSCHANGED handler (win_wndproc.cpp): the renderer
// sizes its viewport from glConfig.nativeScreenWidth/Height, so without this a
// WM fullscreen/resize leaves the game drawing into the old rectangle.
void GLimp_HandleWindowResize( int width, int height ) {
	if ( !R_IsInitialized() || width <= 0 || height <= 0 ) {
		return;
	}
	int drawableW = width;
	int drawableH = height;
	if ( sysLocalSDL.window != nullptr ) {
		SDL_GL_GetDrawableSize( sysLocalSDL.window, &drawableW, &drawableH );
	}
	if ( drawableW <= 0 || drawableH <= 0 ) {
		return;
	}
	glConfig.nativeScreenWidth = drawableW;
	glConfig.nativeScreenHeight = drawableH;
	if ( !glConfig.isFullscreen ) {
		r_windowWidth.SetInteger( drawableW );
		r_windowHeight.SetInteger( drawableH );
	}
}

bool GLimp_Init( glimpParms_t parms ) {
	// Create window and OpenGL context
	const char* title = "Doom 3 BFG Edition";
	int width = parms.width;
	int height = parms.height;
	bool fullscreen = (parms.fullScreen > 0);
	bool vsync = true; // Default to vsync on

	if (!sysLocalSDL.CreateWindow(title, width, height, fullscreen, vsync, parms.x, parms.y)) {
		return false;
	}

	GLimp_UpdateScreenParms( parms );

	// VSync is controlled via r_vsync cvar after init
	return true;
}

void GLimp_Shutdown() {
	sysLocalSDL.DestroyWindow();
}

void GLimp_SwapBuffers() {
	SDL_GL_SwapWindow( sysLocalSDL.window );
}

void GLimp_SetGamma( unsigned short red[256], unsigned short green[256], unsigned short blue[256] ) {
	// SDL doesn't have direct gamma ramp control in SDL2
	// Could use SDL_SetWindowBrightness but that's for the whole window
}

void GLimp_EnableLogging( bool enable ) {
	// Not implemented
}

bool GLimp_SetScreenParms( glimpParms_t parms ) {
	// tr_local.h declares this as returning bool; the old void definition only
	// linked by accident (return type is not mangled) and left glConfig stale.
	if ( sysLocalSDL.window == nullptr ) {
		return false;
	}

	const bool fullscreen = ( parms.fullScreen != 0 );
	if ( fullscreen ) {
		// covers both "fullscreen on monitor N" and -1 (borderless spanning):
		// use the desktop mode at the window's current display
		SDL_SetWindowFullscreen( sysLocalSDL.window, SDL_WINDOW_FULLSCREEN_DESKTOP );
	} else {
		SDL_SetWindowFullscreen( sysLocalSDL.window, 0 );
		SDL_SetWindowSize( sysLocalSDL.window, parms.width, parms.height );
		// honor r_windowX/r_windowY when set (>= 0); only center on the whole desktop otherwise
		SDL_SetWindowPosition( sysLocalSDL.window,
			( parms.x >= 0 ) ? parms.x : SDL_WINDOWPOS_CENTERED,
			( parms.y >= 0 ) ? parms.y : SDL_WINDOWPOS_CENTERED );
	}

	GLimp_UpdateScreenParms( parms );

	// VSync is controlled via r_vsync cvar
	return true;
}

void GLimp_ActivateContext() {
	SDL_GL_MakeCurrent( sysLocalSDL.window, sysLocalSDL.glContext );
}

void GLimp_DeactivateContext() {
	SDL_GL_MakeCurrent( sysLocalSDL.window, nullptr );
}

bool GLimp_SpawnRenderThread( void (*function)() ) {
	// Not implemented for SDL - single threaded rendering
	return false;
}

void * GLimp_BackEndSleep() {
	// Not implemented
	return nullptr;
}

void GLimp_FrontEndSleep() {
	// Not implemented
}

void GLimp_WakeBackEnd( void *data ) {
	// Not implemented
}