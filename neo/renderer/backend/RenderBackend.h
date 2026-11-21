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
but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#ifndef __RENDERBACKEND_H__
#define __RENDERBACKEND_H__

#include "../idlib/precompiled.h"

class viewDef_t;
struct viewLight_t;
struct drawSurf_t;

// Main render backend orchestrator - coordinates all backend passes
class RenderBackend {
public:
    RenderBackend();
    ~RenderBackend();

    // Initialize backend modules
    void Init();

    // Shutdown backend modules
    void Shutdown();

    // Called each frame to execute all backend passes
    // viewLights is a dense array of numViewLights pointers (see ShadowMaps.h)
    // Returns GPU time in microseconds
    int Execute(viewDef_t* viewDef, viewLight_t** viewLights, int numViewLights, const drawSurf_t** drawSurfs, int numDrawSurfs);

private:
    bool m_Initialized = false;
};

// Global backend hooks, called from the renderer:
//   RenderBackend_Init()      — once per GL (re)init, after the context exists (R_InitOpenGL)
//   RenderBackend_Shutdown()  — before the GL context is destroyed (vid_restart / ShutdownOpenGL)
//   RenderBackend_ExecuteView(viewDef) — once per 3D view, before interaction rendering (RB_DrawViewInternal)
void RenderBackend_Init();
void RenderBackend_Shutdown();
void RenderBackend_ExecuteView( viewDef_t *viewDef );

#endif // __RENDERBACKEND_H__