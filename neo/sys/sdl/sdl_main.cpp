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
GNU General Public License for details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include "sdl_local.h"
#include "../../idlib/precompiled.h"
#include "../../framework/Common.h"
#include "../../framework/CVarSystem.h"
#include <SDL.h>

static idCVar sys_debugMainLoop( "sys_debugMainLoop", "0", CVAR_BOOL, "log every main-loop iteration to stdout (Linux port)" );

/*
==================
main
==================
*/
int main( int argc, char **argv ) {
	// Initialize SDL
	if ( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER ) < 0 ) {
		idLib::Error( "SDL_Init failed: %s", SDL_GetError() );
		return 1;
	}

	// Get command line
	idStr sys_cmdline;
	for ( int i = 1; i < argc; i++ ) {
		if ( i > 1 ) {
			sys_cmdline += " ";
		}
		sys_cmdline += argv[i];
	}

	// Initialize common framework
	printf("main: calling common->Init\n");
	fflush(stdout);
	common->Init( argc, argv, sys_cmdline );
	printf("main: common->Init returned\n");
	fflush(stdout);

	printf("main: entering main loop\n");
	fflush(stdout);
	// Main game loop
	while ( 1 ) {
		const bool trace = sys_debugMainLoop.GetBool();
		if ( trace ) {
			printf("main: calling sysLocalSDL.ProcessEvents\n");
			fflush(stdout);
		}
		// Process SDL events (handled by sys layer)
		sysLocalSDL.ProcessEvents();
		if ( trace ) {
			printf("main: sysLocalSDL.ProcessEvents returned\n");
			fflush(stdout);
			printf("main: calling common->Frame\n");
			fflush(stdout);
		}
		// Run game frame
		common->Frame();
		if ( trace ) {
			printf("main: common->Frame returned\n");
			fflush(stdout);
		}
	}

	// Never gets here
	SDL_Quit();
	return 0;
}
