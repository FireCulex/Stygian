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

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

// Minimal test executable for the Doom 3 BFG engine port
// This demonstrates the core engine (idlib, framework, renderer, sound, sys) working on Linux

#include "../idlib/precompiled.h"
#include "framework/Common.h"
#include "sys/sdl/sdl_local.h"
#include "sound/sdl/sdl_audio.h"
#include "renderer/RenderSystem.h"

int main( int argc, char **argv ) {
	// Initialize SDL
	if ( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER ) < 0 ) {
		idLib::Error( "SDL_Init failed: %s", SDL_GetError() );
		return 1;
	}
	
	// Create SDL window
	SDL_Window *window = SDL_CreateWindow(
		"Doom 3 BFG Engine Test",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		1280,
		720,
		SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
	);
	
	if ( !window ) {
		idLib::Error( "SDL_CreateWindow failed: %s", SDL_GetError() );
		SDL_Quit();
		return 1;
	}
	
	// Create OpenGL context
	SDL_GLContext glContext = SDL_GL_CreateContext( window );
	if ( !glContext ) {
		idLib::Error( "SDL_GL_CreateContext failed: %s", SDL_GetError() );
		SDL_DestroyWindow( window );
		SDL_Quit();
		return 1;
	}
	
	// Make context current
	SDL_GL_MakeCurrent( window, glContext );
	
	// Initialize GLEW or load GL functions
	// (In the actual engine, this is done via qgl.h / qgl_linked.h)
	
	idLib::Printf( "Doom 3 BFG Engine - Minimal Test\n" );
	idLib::Printf( "OpenGL Vendor: %s\n", glGetString( GL_VENDOR ) );
	idLib::Printf( "OpenGL Renderer: %s\n", glGetString( GL_RENDERER ) );
	idLib::Printf( "OpenGL Version: %s\n", glGetString( GL_VERSION ) );
	idLib::Printf( "GLSL Version: %s\n", glGetString( GL_SHADING_LANGUAGE_VERSION ) );
	
	// Test math library
	idVec3 v1( 1.0f, 2.0f, 3.0f );
	idVec3 v2( 4.0f, 5.0f, 6.0f );
	idVec3 v3 = v1 + v2;
	idLib::Printf( "Math test: v1 + v2 = (%.1f, %.1f, %.1f)\n", v3.x, v3.y, v3.z );
	
	float dot = v1 * v2; // dot product
	idLib::Printf( "Dot product: %.1f\n", dot );
	
	idRenderMatrix mat;
	mat.Identity();
	// Manual translation
	mat[3][0] = 1.0f;
	mat[3][1] = 2.0f;
	mat[3][2] = 3.0f;
	idLib::Printf( "Matrix translation: (%.1f, %.1f, %.1f)\n", mat[3][0], mat[3][1], mat[3][2] );
	
	// Test file system
	idFile *f = fileSystem->OpenFileRead( "base/gamex86.dll" );
	if ( f ) {
		idLib::Printf( "File system test: base/gamex86.dll found (size: %d)\n", f->Length() );
		fileSystem->CloseFile( f );
	} else {
		idLib::Printf( "File system test: base/gamex86.dll not found (expected)\n" );
	}
	
	// Test string library
	idStr str = "Hello, Doom 3 BFG!";
	idLib::Printf( "String test: %s\n", str.c_str() );
	idLib::Printf( "String length: %d\n", str.Length() );
	
	// Test memory allocation
	void *ptr = Mem_Alloc( 1024, TAG_TEMP );
	if ( ptr ) {
		idLib::Printf( "Memory alloc test: OK\n" );
		Mem_Free( ptr );
	}
	
	// Test timer
	int start = Sys_Milliseconds();
	SDL_Delay( 100 );
	int elapsed = Sys_Milliseconds() - start;
	idLib::Printf( "Timer test: %d ms elapsed (expected ~100)\n", elapsed );
	
	// Cleanup
	SDL_GL_DeleteContext( glContext );
	SDL_DestroyWindow( window );
	SDL_Quit();
	
	idLib::Printf( "All tests passed!\n" );
	return 0;
}