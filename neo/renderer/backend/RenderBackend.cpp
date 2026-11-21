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

#include "RenderBackend.h"
#include "ShadowMaps.h"
#include "tr_local.h"

RenderBackend::RenderBackend() {
}

RenderBackend::~RenderBackend() {
    Shutdown();
}

void RenderBackend::Init() {
    if (m_Initialized) {
        // The GL context was destroyed and recreated (vid_restart) — release
        // the stale pool before re-initializing.
        Shutdown();
    }

    ShadowMaps::Config config;
    ShadowMaps::Init(config);

    m_Initialized = true;
    common->Printf("RenderBackend: Initialized\n");
}

void RenderBackend::Shutdown() {
    if (!m_Initialized) {
        return;
    }

    ShadowMaps::Shutdown();

    m_Initialized = false;
    common->Printf("RenderBackend: Shutdown\n");
}

int RenderBackend::Execute(viewDef_t* viewDef, viewLight_t** viewLights, int numViewLights,
                           const drawSurf_t** drawSurfs, int numDrawSurfs) {
    if (!m_Initialized) {
        return 0;
    }

    int gpuTime = 0;

    // Assign shadow map pool slots to visible lights
    int assigned = ShadowMaps::AssignSlots(viewDef, viewLights, numViewLights);

    if (assigned > 0) {
        // TODO: Gather shadow casters from frontend
        // For now, pass empty caster list
        const drawSurf_t** casterSurfs = nullptr;
        int numCasterSurfs = 0;

        // Execute shadow map rendering
        ShadowMaps::Execute(viewDef, viewLights, numViewLights, casterSurfs, numCasterSurfs);
    }

    // Debug visualization
    if (ShadowMaps::GetConfig().debugMode > 0) {
        ShadowMaps::DrawDebug(viewDef);
    }

    return gpuTime;
}

// ---------------------------------------------------------------------------
// Global instance + renderer hooks. The renderer calls these from
// R_InitOpenGL / GLimp_Shutdown paths and RB_DrawViewInternal — nothing else
// references RenderBackend, which is why the shadow backend was never
// initialized on the Linux build before.
// ---------------------------------------------------------------------------
static RenderBackend g_RenderBackend;

void RenderBackend_Init() {
    g_RenderBackend.Init();
}

void RenderBackend_Shutdown() {
    g_RenderBackend.Shutdown();
}

void RenderBackend_ExecuteView( viewDef_t *viewDef ) {
    if ( viewDef == nullptr || viewDef->viewLights == nullptr ) {
        return;
    }

    // viewDef->viewLights is a linked list; AssignSlots/Execute want a dense
    // array of pointers. Densify into frame-temp memory.
    int numViewLights = 0;
    for ( viewLight_t *vLight = viewDef->viewLights; vLight != nullptr; vLight = vLight->next ) {
        numViewLights++;
    }
    viewLight_t **lights = (viewLight_t **)R_FrameAlloc( sizeof( viewLight_t * ) * numViewLights, FRAME_ALLOC_VIEW_LIGHT );
    int n = 0;
    for ( viewLight_t *vLight = viewDef->viewLights; vLight != nullptr; vLight = vLight->next ) {
        lights[n++] = vLight;
    }

    g_RenderBackend.Execute( viewDef, lights, n,
                             (const drawSurf_t **)viewDef->drawSurfs, viewDef->numDrawSurfs );
}