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

#ifndef __SHADOWMAPS_H__
#define __SHADOWMAPS_H__

#include "../idlib/precompiled.h"
#include "tr_local.h"

class viewDef_t;
struct viewLight_t;
struct drawSurf_t;

namespace ShadowMaps {

// Pool slot types
enum ShadowMapType {
    SM_NONE = 0,
    SM_OMNI_CUBEMAP,       // Box omni light: 6-face cubemap
    SM_PROJECTED_2D,       // Projected frustum light: single 2D depth map
    SM_DIRECTIONAL_CASCADE // Parallel light: 4-cascade CSM
};
}

// Cvar externs (defined in ShadowMaps.cpp at global scope)
extern idCVar r_useShadowMaps;
extern idCVar r_shadowMapPoolSize;
extern idCVar r_shadowMapResolution;
extern idCVar r_shadowMapCascadeResolution;
extern idCVar r_shadowMapPCFSamples;
extern idCVar r_shadowMapBiasConstant;
extern idCVar r_shadowMapBiasSlope;
extern idCVar r_shadowMapDebug;
extern idCVar r_shadowMapProfile;
extern idCVar r_shadowMapCullFaces;
extern idCVar r_shadowMapRetainStatic;
extern idCVar r_shadowMapScaleDistant;

namespace ShadowMaps {

// Pre-allocated pool slot (created at startup, reused each frame)
struct ShadowMapSlot {
    ShadowMapType   type = SM_NONE;
    GLuint          depthTexture = 0;           // Cubemap, 2D, or cascade array
    GLuint          framebuffers[6] = {0};      // 6 for cubemap faces, 1 for 2D, 4 for cascades
    idRenderMatrix  viewProjections[6];         // Per-face or per-cascade matrices
    float           splitDepths[4] = {0};       // For directional cascades only
    int             resolution = 1024;
    int             ownerLightIndex = -1;       // viewLight index currently using this slot
    int             lastUsedFrame = 0;          // For LRU eviction
    bool            initialized = false;
};

// Per-light shadow reference (parallel array indexed by light index)
struct LightShadowRef {
    int             poolSlot = -1;              // -1 = no shadow map assigned
    bool            valid = false;
    idRenderMatrix  viewProjections[6];         // Copied from slot for shader upload
    ShadowMapType   type = SM_NONE;
    int             resolution = 0;
};

// Shadow map configuration
struct Config {
    int poolSize = 64;              // r_shadowMapPoolSize
    int omniResolution = 1024;      // r_shadowMapResolution
    int cascadeResolution = 2048;   // r_shadowMapCascadeResolution
    int pcfSamples = 3;             // r_shadowMapPCFSamples (1, 3, 5)
    float biasConstant = 0.005f;    // r_shadowMapBiasConstant
    float biasSlope = 2.0f;         // r_shadowMapBiasSlope
    int debugMode = 0;              // r_shadowMapDebug
};

// Initialize the shadow map system (called once at renderer init)
void Init(const Config& config);

// Shutdown and release all resources
void Shutdown();

// Called each frame: assign pool slots to visible lights
// viewLights is a dense array of numViewLights pointers (the engine keeps a
// linked list on viewDef->viewLights — the caller densifies it).
// Returns number of lights that got shadow maps
int AssignSlots(viewDef_t* viewDef, viewLight_t** viewLights, int numViewLights);

// Execute shadow map rendering for all assigned lights
void Execute(viewDef_t* viewDef, viewLight_t** viewLights, int numViewLights, const drawSurf_t** casterSurfs, int numCasterSurfs);

// Get shadow reference for a light index (for backend draw path)
const LightShadowRef* GetLightShadowRef(int lightIndex);

// Get the config (for shader uniform setup)
const Config& GetConfig();

// Get the pool array (for backend draw path)
const ShadowMapSlot* GetPool();

// Debug visualization
void DrawDebug(viewDef_t* viewDef);

} // namespace ShadowMaps

#endif // __SHADOWMAPS_H__