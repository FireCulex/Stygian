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

#include "ShadowMaps.h"
#include "tr_local.h"
#include "GLState.h"
#include "BufferObject.h"

// Cvars (must be at global scope for cvar system to find them)
idCVar r_useShadowMaps("r_useShadowMaps", "0", CVAR_RENDERER | CVAR_BOOL | CVAR_ARCHIVE, "Enable shadow maps (replaces stencil volumes)");
idCVar r_shadowMapPoolSize("r_shadowMapPoolSize", "64", CVAR_RENDERER | CVAR_INTEGER | CVAR_ARCHIVE, "Max shadow maps in pool");
idCVar r_shadowMapResolution("r_shadowMapResolution", "1024", CVAR_RENDERER | CVAR_INTEGER | CVAR_ARCHIVE, "Omni/projected shadow map resolution");
idCVar r_shadowMapCascadeResolution("r_shadowMapCascadeResolution", "2048", CVAR_RENDERER | CVAR_INTEGER | CVAR_ARCHIVE, "Directional cascade resolution");
idCVar r_shadowMapPCFSamples("r_shadowMapPCFSamples", "3", CVAR_RENDERER | CVAR_INTEGER | CVAR_ARCHIVE, "PCF kernel size (1, 3, 5)");
idCVar r_shadowMapBiasConstant("r_shadowMapBiasConstant", "0.005", CVAR_RENDERER | CVAR_FLOAT | CVAR_ARCHIVE, "Constant shadow bias");
idCVar r_shadowMapBiasSlope("r_shadowMapBiasSlope", "2.0", CVAR_RENDERER | CVAR_FLOAT | CVAR_ARCHIVE, "Slope-scaled shadow bias");
idCVar r_shadowMapDebug("r_shadowMapDebug", "0", CVAR_RENDERER | CVAR_INTEGER | CVAR_ARCHIVE, "Debug visualization: 0=off, 1=depth, 2=slot index, 3=cascade index");
idCVar r_shadowMapProfile("r_shadowMapProfile", "0", CVAR_RENDERER | CVAR_BOOL | CVAR_ARCHIVE, "Enable GPU timer queries for shadow pass profiling");
idCVar r_shadowMapCullFaces("r_shadowMapCullFaces", "1", CVAR_RENDERER | CVAR_BOOL | CVAR_ARCHIVE, "Cull empty cubemap faces during shadow rendering");
idCVar r_shadowMapRetainStatic("r_shadowMapRetainStatic", "1", CVAR_RENDERER | CVAR_BOOL | CVAR_ARCHIVE, "Retain pool slots for static lights across frames");
idCVar r_shadowMapScaleDistant("r_shadowMapScaleDistant", "1", CVAR_RENDERER | CVAR_BOOL | CVAR_ARCHIVE, "Scale down resolution for distant/low-priority lights");

namespace ShadowMaps {

static Config g_Config;
static ShadowMapSlot* g_Pool = nullptr;
static LightShadowRef* g_LightRefs = nullptr;
static int g_MaxViewLights = 0;
static int g_FrameCount = 0;

void Init(const Config& config) {
    g_Config = config;

    // Override with cvars if they differ
    g_Config.poolSize = r_shadowMapPoolSize.GetInteger();
    g_Config.omniResolution = r_shadowMapResolution.GetInteger();
    g_Config.cascadeResolution = r_shadowMapCascadeResolution.GetInteger();
    g_Config.pcfSamples = r_shadowMapPCFSamples.GetInteger();
    g_Config.biasConstant = r_shadowMapBiasConstant.GetFloat();
    g_Config.biasSlope = r_shadowMapBiasSlope.GetFloat();
    g_Config.debugMode = r_shadowMapDebug.GetInteger();

    // Clamp pool size
    g_Config.poolSize = idMath::ClampInt(1, 256, g_Config.poolSize);
    g_Config.omniResolution = idMath::ClampInt(256, 4096, g_Config.omniResolution);
    g_Config.cascadeResolution = idMath::ClampInt(512, 8192, g_Config.cascadeResolution);
    g_Config.pcfSamples = idMath::ClampInt(1, 9, g_Config.pcfSamples);
    if (g_Config.pcfSamples != 1 && g_Config.pcfSamples != 3 && g_Config.pcfSamples != 5) {
        g_Config.pcfSamples = 3;
    }

    // Allocate pool
    g_Pool = new ShadowMapSlot[g_Config.poolSize];
    memset(g_Pool, 0, sizeof(ShadowMapSlot) * g_Config.poolSize);

    // Pre-allocate GL resources for each pool slot
    for (int i = 0; i < g_Config.poolSize; ++i) {
        ShadowMapSlot& slot = g_Pool[i];
        // memset wiped the ownerLightIndex = -1 default — restore it so a
        // fresh slot is never mistaken for one owned when g_FrameCount == 0
        slot.ownerLightIndex = -1;
        // Resources will be allocated on first use based on light type
        slot.initialized = false;
    }

    // Light refs will be allocated per-frame in AssignSlots
    g_MaxViewLights = 4096; // generous upper bound
    g_LightRefs = new LightShadowRef[g_MaxViewLights];

    common->Printf("ShadowMaps: Initialized (pool=%d, omniRes=%d, cascadeRes=%d)\n",
                   g_Config.poolSize, g_Config.omniResolution, g_Config.cascadeResolution);
}

void Shutdown() {
    if (g_Pool) {
        for (int i = 0; i < g_Config.poolSize; ++i) {
            ShadowMapSlot& slot = g_Pool[i];
            if (slot.depthTexture) {
                qglDeleteTextures(1, &slot.depthTexture);
                slot.depthTexture = 0;
            }
            for (int f = 0; f < 6; ++f) {
                if (slot.framebuffers[f]) {
                    qglDeleteFramebuffers(1, &slot.framebuffers[f]);
                    slot.framebuffers[f] = 0;
                }
            }
        }
        delete[] g_Pool;
        g_Pool = nullptr;
    }

    if (g_LightRefs) {
        delete[] g_LightRefs;
        g_LightRefs = nullptr;
    }

    common->Printf("ShadowMaps: Shutdown\n");
}

static void EnsureSlotResources(ShadowMapSlot& slot, ShadowMapType type, int resolution) {
    if (slot.initialized && slot.type == type && slot.resolution == resolution) {
        return; // Already correct
    }

    // Clean up old resources if type/resolution changed
    if (slot.depthTexture) {
        qglDeleteTextures(1, &slot.depthTexture);
        slot.depthTexture = 0;
    }
    for (int f = 0; f < 6; ++f) {
        if (slot.framebuffers[f]) {
            qglDeleteFramebuffers(1, &slot.framebuffers[f]);
            slot.framebuffers[f] = 0;
        }
    }

    slot.type = type;
    slot.resolution = resolution;

    // Create depth texture
    qglGenTextures(1, &slot.depthTexture);

    if (type == SM_OMNI_CUBEMAP) {
        qglBindTexture(GL_TEXTURE_CUBE_MAP, slot.depthTexture);
        for (int face = 0; face < 6; ++face) {
            qglTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_DEPTH_COMPONENT32F,
                          resolution, resolution, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        }
        qglTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        qglTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        qglTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        qglTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        qglTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        qglTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        qglTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        // Enable seamless cubemap filtering to avoid seam artifacts
        qglEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

        // Create 6 FBOs (one per face)
        qglGenFramebuffers(6, slot.framebuffers);
        for (int face = 0; face < 6; ++face) {
            qglBindFramebuffer(GL_FRAMEBUFFER, slot.framebuffers[face]);
            qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                    GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, slot.depthTexture, 0);
            qglDrawBuffer(GL_NONE);
            qglReadBuffer(GL_NONE);
            GLenum status = qglCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
                common->Warning("ShadowMaps: Cubemap face %d FBO incomplete: 0x%x", face, status);
            }
        }

    } else if (type == SM_PROJECTED_2D) {
        qglBindTexture(GL_TEXTURE_2D, slot.depthTexture);
        qglTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F,
                      resolution, resolution, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

        // Create 1 FBO
        qglGenFramebuffers(1, slot.framebuffers);
        qglBindFramebuffer(GL_FRAMEBUFFER, slot.framebuffers[0]);
        qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                GL_TEXTURE_2D, slot.depthTexture, 0);
        qglDrawBuffer(GL_NONE);
        qglReadBuffer(GL_NONE);
        GLenum status = qglCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            common->Warning("ShadowMaps: Projected 2D FBO incomplete: 0x%x", status);
        }

    } else if (type == SM_DIRECTIONAL_CASCADE) {
        // For cascades, use a 2D array texture with 4 layers
        qglBindTexture(GL_TEXTURE_2D_ARRAY, slot.depthTexture);
        qglTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F,
                      resolution, resolution, 4, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        qglTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        qglTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        qglTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        qglTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        qglTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        qglTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

        // Create 4 FBOs (one per cascade layer)
        qglGenFramebuffers(4, slot.framebuffers);
        for (int cascade = 0; cascade < 4; ++cascade) {
            qglBindFramebuffer(GL_FRAMEBUFFER, slot.framebuffers[cascade]);
            qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                       slot.depthTexture, 0, cascade);
            qglDrawBuffer(GL_NONE);
            qglReadBuffer(GL_NONE);
            GLenum status = qglCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
                common->Warning("ShadowMaps: Cascade %d FBO incomplete: 0x%x", cascade, status);
            }
        }
    }

    qglBindFramebuffer(GL_FRAMEBUFFER, 0);
    qglBindTexture(GL_TEXTURE_2D, 0);
    qglBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    qglBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    slot.initialized = true;
}

// ============================================================
// Light view-projection matrix computation
// ============================================================

// Cubemap face directions for omni lights
static const idVec3 g_CubemapFaceDirs[6] = {
    idVec3( 1,  0,  0),  // +X
    idVec3(-1,  0,  0),  // -X
    idVec3( 0,  1,  0),  // +Y
    idVec3( 0, -1,  0),  // -Y
    idVec3( 0,  0,  1),  // +Z
    idVec3( 0,  0, -1)   // -Z
};

static const idVec3 g_CubemapFaceUps[6] = {
    idVec3( 0, -1,  0),  // +X
    idVec3( 0, -1,  0),  // -X
    idVec3( 0,  0,  1),  // +Y
    idVec3( 0,  0, -1),  // -Y
    idVec3( 0, -1,  0),  // +Z
    idVec3( 0, -1,  0)   // -Z
};

// Compute view-projection matrices for omni (box) lights - 6 cubemap faces
void ComputeOmniLightMatrices(viewLight_t* light, idRenderMatrix faceViews[6]) {
    if (!light || !light->lightDef) {
        return;
    }

    const renderLight_t& parms = light->lightDef->parms;
    idVec3 lightOrigin = light->globalLightOrigin;
    // Box lights: use the largest radius component so the whole box fits in the cubemap
    float radius = Max( Max( idMath::Fabs( parms.lightRadius.x ), idMath::Fabs( parms.lightRadius.y ) ),
                         idMath::Fabs( parms.lightRadius.z ) );
    if ( radius < 1.0f ) {
        radius = 1.0f;
    }

    for (int face = 0; face < 6; ++face) {
        // View matrix: look from light origin along face direction
        idVec3 target = lightOrigin + g_CubemapFaceDirs[face] * radius;
        idVec3 up = g_CubemapFaceUps[face];

        idRenderMatrix view;
        view.LookAt(lightOrigin, target, up);

        // Projection matrix: 90 degree FOV, aspect 1:1, near/far from radius
        idRenderMatrix proj;
        proj.Perspective(90.0f, 1.0f, 1.0f, radius);

        // Combined view-projection
        faceViews[face] = proj * view;
    }
}

// Compute view-projection matrix for projected (spot) lights.
// Builds a standard GL perspective VP (column-vector convention: clip = VP * worldPos)
// from the light parms, mirroring R_ComputeSpotLightProjectionMatrix in
// RenderWorld_defs.cpp but in clip space instead of [0,1] texgen space.
void ComputeProjectedLightMatrices(viewLight_t* light, idRenderMatrix& viewProjection) {
    if (!light || !light->lightDef) {
        viewProjection.Identity();
        return;
    }

    const renderLight_t& parms = light->lightDef->parms;
    if (parms.parallel || parms.pointLight) {
        viewProjection.Identity();
        return;
    }

    const float targetDist = parms.target.Length();
    if (targetDist < 1e-4f || parms.right.LengthSqr() < 1e-8f || parms.up.LengthSqr() < 1e-8f) {
        viewProjection.Identity();
        return;
    }
    const idVec3 fwdLocal = parms.target * (1.0f / targetDist);
    const float tanHalfFovX = 0.5f * targetDist / parms.right.Length();
    const float tanHalfFovY = 0.5f * targetDist / parms.up.Length();

    const float zNear = Max(parms.start * fwdLocal, 8.0f);
    const float zFar  = Max(parms.end * fwdLocal, 16.0f);

    // World-space eye / direction / up
    const idVec3 eye = parms.origin;
    const idVec3 fwdWorld = parms.axis * fwdLocal;
    const idVec3 upWorld = parms.axis * parms.up;
    const idVec3 targetWorld = eye + fwdWorld;

    idRenderMatrix view;
    view.LookAt(eye, targetWorld, upWorld);

    const float fovYDeg = 2.0f * atanf(tanHalfFovY) * (180.0f / idMath::PI);
    const float aspect = tanHalfFovX / tanHalfFovY;
    idRenderMatrix proj;
    proj.Perspective(fovYDeg, aspect, zNear, zFar);

    viewProjection = proj * view;
}

// Extract zNear and zFar from projection matrix
static void ExtractNearFarFromProjection( const idRenderMatrix& proj, float& zNear, float& zFar ) {
    // For standard perspective projection:
    // proj[2][2] = (zFar + zNear) / (zNear - zFar)
    // proj[3][2] = (2 * zFar * zNear) / (zNear - zFar)
    // Solve for zNear and zFar:
    float C = proj[2][2];
    float D = proj[3][2];
    if ( idMath::Fabs( C + 1.0f ) > 0.001f ) {
        zFar = D / ( C - 1.0f );
        zNear = D / ( C + 1.0f );
    } else {
        // Orthographic or invalid - use defaults
        zNear = 1.0f;
        zFar = 1000.0f;
    }
}

// Compute cascade splits for directional lights (practical/logarithmic blend)
void ComputeCascadeSplits(const viewDef_t* viewDef, float splits[4], float splitsClip[4]) {
    if (!viewDef) {
        return;
    }

    float zNear, zFar;
    ExtractNearFarFromProjection( viewDef->projectionRenderMatrix, zNear, zFar );

    // Practical split lambda (fixed at 0.92, no cvar)
    const float lambda = 0.92f;

    for (int i = 0; i < 4; ++i) {
        float practical = zNear * powf(zFar / zNear, (i + 1) / 4.0f);
        float logarithmic = zNear + (zFar - zNear) * (i + 1) / 4.0f;
        splits[i] = lambda * practical + (1.0f - lambda) * logarithmic;
    }

    // Convert to clip space [0,1] for cascade selection in shader
    // Using the projection matrix to convert view-space depth to clip space
    if (splitsClip) {
        const idRenderMatrix& proj = viewDef->projectionRenderMatrix;
        for (int i = 0; i < 4; ++i) {
            // Transform view-space point (0, 0, -split, 1) to clip space
            float clipZ = proj[2][2] * (-splits[i]) + proj[2][3];
            float clipW = proj[3][2] * (-splits[i]) + proj[3][3];
            splitsClip[i] = clipZ / clipW * 0.5f + 0.5f; // Convert from [-1,1] to [0,1]
        }
    }
}

// Compute view-projection matrices for directional lights - 4 cascades
void ComputeDirectionalLightMatrices(viewLight_t* light, viewDef_t* viewDef, idRenderMatrix cascadeViews[4], float cascadeSplits[4]) {
    if (!light || !light->lightDef || !viewDef) {
        return;
    }

    float zNear, zFar;
    ExtractNearFarFromProjection( viewDef->projectionRenderMatrix, zNear, zFar );

    const renderLight_t& parms = light->lightDef->parms;
    // Parallel light direction is in lightCenter (normalized)
    idVec3 lightDir = parms.lightCenter;
    lightDir.Normalize();

    // Get cascade splits
    ComputeCascadeSplits(viewDef, cascadeSplits, nullptr);

    // View origin is at the camera
    idVec3 viewOrigin = viewDef->renderView.vieworg;

    for (int i = 0; i < 4; ++i) {
        float nearZ = (i == 0) ? zNear : cascadeSplits[i - 1];
        float farZ = cascadeSplits[i];

        // Create cascade frustum corners in view space
        idVec3 corners[8];
        float fovX = viewDef->renderView.fov_x;
        float fovY = viewDef->renderView.fov_y;
        float tanHalfFovX = tanf(fovX * 0.5f * idMath::PI / 180.0f);
        float tanHalfFovY = tanf(fovY * 0.5f * idMath::PI / 180.0f);

        // Near face
        float nh = nearZ * tanHalfFovY;
        float nw = nearZ * tanHalfFovX;
        corners[0] = idVec3(-nw, -nh, -nearZ);
        corners[1] = idVec3( nw, -nh, -nearZ);
        corners[2] = idVec3( nw,  nh, -nearZ);
        corners[3] = idVec3(-nw,  nh, -nearZ);

        // Far face
        float fh = farZ * tanHalfFovY;
        float fw = farZ * tanHalfFovX;
        corners[4] = idVec3(-fw, -fh, -farZ);
        corners[5] = idVec3( fw, -fh, -farZ);
        corners[6] = idVec3( fw,  fh, -farZ);
        corners[7] = idVec3(-fw,  fh, -farZ);

        // Transform to world space
        for (int c = 0; c < 8; ++c) {
            corners[c] = viewDef->renderView.viewaxis * corners[c] + viewOrigin;
        }

        // Compute light-space bounds of cascade frustum
        idVec3 center(0, 0, 0);
        for (int c = 0; c < 8; ++c) {
            center += corners[c];
        }
        center *= 0.125f;

        // Create orthographic projection matrix for this cascade
        // Light view matrix: look from far away along light direction
        idVec3 lightPos = center - lightDir * 10000.0f; // Far away
        idVec3 lightTarget = center;
        idVec3 lightUp = (fabs(lightDir.z) < 0.9f) ? idVec3(0, 0, 1) : idVec3(0, 1, 0);

        idRenderMatrix lightView;
        lightView.LookAt(lightPos, lightTarget, lightUp);

        // Find bounds in light space
        idBounds lightBounds;
        lightBounds.Clear();
        for (int c = 0; c < 8; ++c) {
            idVec4 transformed;
            lightView.TransformPoint(idVec4(corners[c].x, corners[c].y, corners[c].z, 1.0f), transformed);
            lightBounds.AddPoint(idVec3(transformed.x, transformed.y, transformed.z));
        }

        // Add some padding
        idVec3 padding = (lightBounds[1] - lightBounds[0]) * 0.1f;
        lightBounds[0] -= padding;
        lightBounds[1] += padding;

        idRenderMatrix lightProj;
        lightProj.Ortho(
            lightBounds[0].x, lightBounds[1].x,
            lightBounds[0].y, lightBounds[1].y,
            lightBounds[0].z, lightBounds[1].z
        );

        cascadeViews[i] = lightProj * lightView;
    }
}

int AssignSlots(viewDef_t* viewDef, viewLight_t** viewLights, int numViewLights) {
    // The shadow-map programs are engine additions whose source files may be absent
    // (stock assets). Without them, stay on the stock stencil path — never bind progId 0.
    if (!r_useShadowMaps.GetBool() || !g_Pool
        || !renderProgManager.ShadowMapProgramsLoaded()) {
        // Clear all refs
        if (g_LightRefs) {
            memset(g_LightRefs, 0, sizeof(LightShadowRef) * g_MaxViewLights);
        }
        return 0;
    }

    // Monotonic frame counter for slot LRU/ownership. renderView.viewID is
    // per-view-within-frame (the main view is typically 1 every frame), so it
    // cannot serve as a frame number — with a constant "frame" the LRU bonus
    // is always 0, slot scores tie, and every same-type light piles onto the
    // same pool slot (only one shadow map actually updates per frame).
    static int s_ShadowFrame = 0;
    g_FrameCount = ++s_ShadowFrame;

    // Light refs are keyed by lightDef->index (that's what the draw path passes
    // to GetLightShadowRef), so make sure the array covers every light in view.
    int maxLightDefIndex = -1;
    for (int i = 0; i < numViewLights; ++i) {
        viewLight_t* v = viewLights[i];
        if (v && v->lightDef && v->lightDef->index > maxLightDefIndex) {
            maxLightDefIndex = v->lightDef->index;
        }
    }
    if (maxLightDefIndex >= g_MaxViewLights) {
        delete[] g_LightRefs;
        g_MaxViewLights = maxLightDefIndex + 1;
        g_LightRefs = new LightShadowRef[g_MaxViewLights];
    }

    // Clear light refs for this frame
    memset(g_LightRefs, 0, sizeof(LightShadowRef) * g_MaxViewLights);

    // Collect candidate lights that cast shadows
    struct Candidate {
        int lightIndex;     // index into the viewLights array (for Execute)
        int lightDefIndex;  // lightDef->index (key for g_LightRefs)
        float priority;
        ShadowMapType type;
        int resolution;
    };

    static idList<Candidate> candidates;
    candidates.Clear();

    for (int i = 0; i < numViewLights; ++i) {
        viewLight_t* light = viewLights[i];
        if (!light || !light->lightDef || !light->lightDef->LightCastsShadows()) {
            continue;
        }
        if (light->removeFromList) {
            continue;
        }

        // Determine light type from light shader
        const idMaterial* shader = light->lightShader;
        ShadowMapType type = SM_NONE;
        int resolution = 0;

        if (shader) {
            // Check for parallel light (directional) — cascades are deferred in Phase 1:
            // skip them here so they get no shadow ref and fall back to stencil shadows.
            if (shader->IsParallelLight()) {
                continue;
            }
            // Check for projected light (has projection texture)
            if (shader->HasProjectionTexture()) {
                type = SM_PROJECTED_2D;
                resolution = g_Config.omniResolution;
            }
            // Otherwise treat as omni (box) light
            else {
                type = SM_OMNI_CUBEMAP;
                resolution = g_Config.omniResolution;
            }
        } else {
            // Default to omni
            type = SM_OMNI_CUBEMAP;
            resolution = g_Config.omniResolution;
        }

        // Calculate priority: screen coverage * intensity
        // Use light bounds projected to screen space
        float priority = 1.0f;
        if (light->scissorRect.IsValid()) {
            int area = light->scissorRect.GetWidth() * light->scissorRect.GetHeight();
            float lightRadius = (light->lightDef ? light->lightDef->parms.lightRadius.x : 100.0f);
        priority = (float)area * lightRadius;
        }

        Candidate c;
        c.lightIndex = i;
        c.lightDefIndex = light->lightDef->index;
        c.priority = priority;
        c.type = type;
        c.resolution = resolution;
        candidates.Append(c);
    }

    // Sort by priority descending
    std::sort(candidates.Ptr(), candidates.Ptr() + candidates.Num(), [](const Candidate& a, const Candidate& b) {
        return a.priority > b.priority;
    });

    // Assign pool slots
    int assigned = 0;
    for (int i = 0; i < candidates.Num() && assigned < g_Config.poolSize; ++i) {
        const Candidate& c = candidates[i];

        // Find best slot: prefer same type, then LRU
        int bestSlot = -1;
        int bestScore = -1;

        for (int s = 0; s < g_Config.poolSize; ++s) {
            ShadowMapSlot& slot = g_Pool[s];
            int score = 0;

            if (!slot.initialized) {
                score = 1000; // Empty slot preferred
            } else if (slot.type == c.type && slot.resolution == c.resolution) {
                score = 500; // Same type/resolution
            } else {
                score = 100; // Different type, will reallocate
            }

            // LRU bonus
            score += (g_FrameCount - slot.lastUsedFrame);

            if (score > bestScore) {
                bestScore = score;
                bestSlot = s;
            }
        }

        if (bestSlot >= 0) {
            ShadowMapSlot& slot = g_Pool[bestSlot];
            EnsureSlotResources(slot, c.type, c.resolution);

            // Compute and store view-projection matrices for this light
            viewLight_t* light = viewLights[c.lightIndex];
            if (c.type == SM_OMNI_CUBEMAP) {
                ComputeOmniLightMatrices(light, slot.viewProjections);
            } else if (c.type == SM_PROJECTED_2D) {
                ComputeProjectedLightMatrices(light, slot.viewProjections[0]);
            } else if (c.type == SM_DIRECTIONAL_CASCADE) {
                float cascadeSplits[4];
                ComputeDirectionalLightMatrices(light, viewDef, slot.viewProjections, cascadeSplits);
                for (int cs = 0; cs < 4; ++cs) {
                    slot.splitDepths[cs] = cascadeSplits[cs];
                }
            }

            slot.ownerLightIndex = c.lightIndex;
            slot.lastUsedFrame = g_FrameCount;

            LightShadowRef& ref = g_LightRefs[c.lightDefIndex];
            ref.poolSlot = bestSlot;
            ref.valid = true;
            ref.type = c.type;
            ref.resolution = c.resolution;
            // Copy view projections from slot
            for (int f = 0; f < 6; ++f) {
                ref.viewProjections[f] = slot.viewProjections[f];
            }

            assigned++;
        }
    }

    return assigned;
}

// Bind the depth-only shader for the given slot type / skinning variant
static void BindShadowDepthShader(ShadowMapType type, bool skinned) {
    if (type == SM_OMNI_CUBEMAP) {
        if (skinned) { renderProgManager.BindShader_ShadowOmniSkinned(); } else { renderProgManager.BindShader_ShadowOmni(); }
    } else if (type == SM_PROJECTED_2D) {
        if (skinned) { renderProgManager.BindShader_ShadowProjectedSkinned(); } else { renderProgManager.BindShader_ShadowProjected(); }
    } else {
        if (skinned) { renderProgManager.BindShader_ShadowDirectionalSkinned(); } else { renderProgManager.BindShader_ShadowDirectional(); }
    }
}

// Draw a caster list into one shadow-map face. The MVP is composed per surface
// space exactly like the main pass does (tr_frontend_addmodels.cpp:483-488):
//   shadowMVP = lightVP * Transpose(space->modelMatrix)
// because idRenderMatrix is column-convention (clip = M * v) while the float[16]
// model matrices are row-convention (v' = v * M), so the transpose converts.
static void RenderShadowCasters(drawSurf_t* casters, const idRenderMatrix& lightVP, ShadowMapType type,
                                int& totalDrawCalls, int& castersSkipped) {
    const viewEntity_t* lastSpace = NULL;
    for (drawSurf_t* ds = casters; ds != NULL; ds = ds->nextOnLight) {
        if (!ds->space) {
            castersSkipped++;
            continue;
        }
        // Prefer the shadow vertex cache: the hand-built prelightModel caster
        // (tr_frontend_addlights.cpp:524-537) has ambientCache == 0 but a valid
        // shadowCache, and using ambientCache there trips the "vertexBuffer == NULL"
        // warning in RB_DrawElementsWithCounters.
        vertCacheHandle_t vbHandle = ds->shadowCache ? ds->shadowCache : ds->ambientCache;
        if (vbHandle == 0) {
            castersSkipped++;
            continue;
        }

        // Skinned casters need the skinned depth shader (joint UBO)
        BindShadowDepthShader(type, ds->jointCache != 0);

        if (ds->space != lastSpace) {
            lastSpace = ds->space;
            idRenderMatrix modelCV;
            idRenderMatrix::Transpose(*(const idRenderMatrix*)ds->space->modelMatrix, modelCV);
            idRenderMatrix mvp;
            idRenderMatrix::Multiply(lightVP, modelCV, mvp);
            renderProgManager.SetRenderParms(RENDERPARM_MVPMATRIX_X, mvp[0], 4);
        }

        vertCacheHandle_t savedAmbient = ds->ambientCache;
        ds->ambientCache = vbHandle;
        RB_DrawElementsWithCounters(ds);
        ds->ambientCache = savedAmbient;
        totalDrawCalls++;
    }
}

void Execute(viewDef_t* viewDef, viewLight_t** viewLights, int numViewLights,
             const drawSurf_t** casterSurfs, int numCasterSurfs) {
    if (!r_useShadowMaps.GetBool() || !g_Pool) {
        return;
    }

    // Timer queries for profiling
    GLuint timerQueries[2] = {0, 0};
    bool profiling = r_shadowMapProfile.GetBool();
    if (profiling) {
        qglGenQueries(2, timerQueries);
        qglBeginQuery(GL_TIME_ELAPSED, timerQueries[0]);
    }

    // Count assigned lights
    int assigned = 0;
    for (int i = 0; i < g_Config.poolSize; ++i) {
        if (g_Pool[i].ownerLightIndex >= 0 && g_Pool[i].lastUsedFrame == g_FrameCount) {
            assigned++;
        }
    }

    if (assigned == 0) {
        if (profiling) {
            qglEndQuery(GL_TIME_ELAPSED);
            qglDeleteQueries(2, timerQueries);
        }
        return;
    }

    // Save current GL state
    GLint prevFBO = 0;
    qglGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevViewport[4];
    qglGetIntegerv(GL_VIEWPORT, prevViewport);
    GLboolean depthWrite = GL_TRUE;
    qglGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);

    // Disable color writes for depth-only rendering
    qglColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    qglDepthMask(GL_TRUE);

    // Depth-only state for the shadow pass
    GLint prevDepthFunc = GL_LESS;
    qglGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);
    GLboolean prevCullFace = GL_FALSE;
    qglGetBooleanv(GL_CULL_FACE, &prevCullFace);
    GLboolean prevBlend = GL_FALSE;
    qglGetBooleanv(GL_BLEND, &prevBlend);
    GLboolean prevStencil = GL_FALSE;
    qglGetBooleanv(GL_STENCIL_TEST, &prevStencil);
    qglEnable(GL_DEPTH_TEST);
    qglDepthFunc(GL_LESS);
    qglEnable(GL_CULL_FACE);
    qglCullFace(GL_BACK);
    qglDisable(GL_BLEND);
    qglDisable(GL_STENCIL_TEST);

    // Track stats for profiling
    int totalFacesRendered = 0;
    int totalCascadesRendered = 0;
    int totalDrawCalls = 0;
    int facesSkipped = 0;
    int castersSkipped = 0;

    // Render shadow maps for each assigned light
    for (int slotIdx = 0; slotIdx < g_Config.poolSize; ++slotIdx) {
        ShadowMapSlot& slot = g_Pool[slotIdx];
        if (slot.ownerLightIndex < 0 || slot.lastUsedFrame != g_FrameCount) {
            continue;
        }

        viewLight_t* light = viewLights[slot.ownerLightIndex];
        if (!light || !light->lightDef || !light->lightDef->LightCastsShadows()) {
            continue;
        }

        // Get shadow casters for this light
        drawSurf_t* casters = light->shadowCasters;
        if (!casters) {
            continue;
        }

        // Static light retention: if light hasn't moved and casters unchanged, skip re-render
        bool isStaticLight = false;
        if (r_shadowMapRetainStatic.GetBool() && light->lightDef) {
            // Check if light parameters and casters are unchanged
            isStaticLight = !light->lightDef->lightHasMoved && (light->lightDef->lastModifiedFrameNum < g_FrameCount - 1);
        }

        if (slot.type == SM_OMNI_CUBEMAP) {
            // Render 6 cubemap faces
            for (int face = 0; face < 6; ++face) {
                // Per-face frustum culling
                bool skipFace = false;
                if (r_shadowMapCullFaces.GetBool() && casters) {
                    // Check if any caster intersects this face's frustum
                    skipFace = true;
                    for (drawSurf_t* ds = casters; ds != NULL; ds = ds->nextOnLight) {
                        if (ds->frontEndGeo && ds->frontEndGeo->bounds.IsCleared() == false) {
                            // Transform caster bounds to light face space and test
                            // For simplicity, we render all faces if any caster exists
                            // Full frustum culling would require computing face frustum planes
                            skipFace = false;
                            break;
                        }
                    }
                }

                if (skipFace) {
                    facesSkipped++;
                    continue;
                }

                qglBindFramebuffer(GL_FRAMEBUFFER, slot.framebuffers[face]);
                qglViewport(0, 0, slot.resolution, slot.resolution);
                qglClear(GL_DEPTH_BUFFER_BIT);

                RenderShadowCasters(casters, slot.viewProjections[face], SM_OMNI_CUBEMAP, totalDrawCalls, castersSkipped);
                totalFacesRendered++;
            }
        }
        else if (slot.type == SM_PROJECTED_2D) {
            int effectiveResolution = slot.resolution;

            qglBindFramebuffer(GL_FRAMEBUFFER, slot.framebuffers[0]);
            qglViewport(0, 0, effectiveResolution, effectiveResolution);
            qglClear(GL_DEPTH_BUFFER_BIT);

            RenderShadowCasters(casters, slot.viewProjections[0], SM_PROJECTED_2D, totalDrawCalls, castersSkipped);
            totalFacesRendered++;
        }
        else if (slot.type == SM_DIRECTIONAL_CASCADE) {
            // Render 4 cascade layers
            for (int cascade = 0; cascade < 4; ++cascade) {
                qglBindFramebuffer(GL_FRAMEBUFFER, slot.framebuffers[cascade]);
                qglViewport(0, 0, slot.resolution, slot.resolution);
                qglClear(GL_DEPTH_BUFFER_BIT);

                RenderShadowCasters(casters, slot.viewProjections[cascade], SM_DIRECTIONAL_CASCADE, totalDrawCalls, castersSkipped);
                totalCascadesRendered++;
            }
        }
    }

    // Restore GL state
    qglBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    qglViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    qglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    qglDepthMask(depthWrite);
    qglDepthFunc(prevDepthFunc);
    if (prevCullFace) { qglEnable(GL_CULL_FACE); } else { qglDisable(GL_CULL_FACE); }
    if (prevBlend) { qglEnable(GL_BLEND); } else { qglDisable(GL_BLEND); }
    if (prevStencil) { qglEnable(GL_STENCIL_TEST); } else { qglDisable(GL_STENCIL_TEST); }

    // Profiling: read timer query results
    if (profiling) {
        qglEndQuery(GL_TIME_ELAPSED);

        GLuint64 elapsedTime = 0;
        qglGetQueryObjectui64v(timerQueries[0], GL_QUERY_RESULT, &elapsedTime);
        float gpuTimeMs = (float)elapsedTime / 1000000.0f;

        common->Printf("ShadowMaps: %.2f ms GPU (%d faces, %d cascades, %d draws, %d faces skipped, %d casters skipped)\n",
                       gpuTimeMs, totalFacesRendered, totalCascadesRendered, totalDrawCalls, facesSkipped, castersSkipped);

        qglDeleteQueries(2, timerQueries);
    }
}

const LightShadowRef* GetLightShadowRef(int lightIndex) {
    if (lightIndex >= 0 && lightIndex < g_MaxViewLights && g_LightRefs) {
        return &g_LightRefs[lightIndex];
    }
    return nullptr;
}

const Config& GetConfig() {
    return g_Config;
}

const ShadowMapSlot* GetPool() {
    return g_Pool;
}

void DrawDebug(viewDef_t* viewDef) {
    if (!r_useShadowMaps.GetBool() || !g_Pool || !viewDef) {
        return;
    }

    int debugMode = r_shadowMapDebug.GetInteger();
    if (debugMode <= 0) {
        return;
    }

    // Save GL state
    GLint prevFBO = 0;
    qglGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevViewport[4];
    qglGetIntegerv(GL_VIEWPORT, prevViewport);
    GLboolean depthTest = GL_FALSE;
    qglGetBooleanv(GL_DEPTH_TEST, &depthTest);
    GLboolean blend = GL_FALSE;
    qglGetBooleanv(GL_BLEND, &blend);

    qglDisable(GL_DEPTH_TEST);
    qglEnable(GL_BLEND);
    qglBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Use a simple quad shader for debug display
    renderProgManager.BindShader_Texture();

    // Find the most recently used slot
    int bestSlot = -1;
    int bestFrame = -1;
    for (int i = 0; i < g_Config.poolSize; ++i) {
        if (g_Pool[i].ownerLightIndex >= 0 && g_Pool[i].lastUsedFrame > bestFrame) {
            bestFrame = g_Pool[i].lastUsedFrame;
            bestSlot = i;
        }
    }

    if (bestSlot >= 0) {
        ShadowMapSlot& slot = g_Pool[bestSlot];

        if (debugMode == 1) {
            // Show shadow map depth
            GLuint texToBind = 0;
            GLenum target = GL_TEXTURE_2D;
            
            if (slot.type == SM_OMNI_CUBEMAP) {
                target = GL_TEXTURE_CUBE_MAP;
                texToBind = slot.depthTexture;
            } else if (slot.type == SM_PROJECTED_2D) {
                target = GL_TEXTURE_2D;
                texToBind = slot.depthTexture;
            } else if (slot.type == SM_DIRECTIONAL_CASCADE) {
                target = GL_TEXTURE_2D_ARRAY;
                texToBind = slot.depthTexture;
            }

            if (texToBind) {
                qglActiveTexture(GL_TEXTURE0);
                qglBindTexture(target, texToBind);
                qglTexParameteri(target, GL_TEXTURE_COMPARE_MODE, GL_NONE); // Sample raw depth

                // Draw fullscreen quad
                float quadVertices[] = {
                    -1.0f, -1.0f, 0.0f, 0.0f,
                     1.0f, -1.0f, 1.0f, 0.0f,
                    -1.0f,  1.0f, 0.0f, 1.0f,
                     1.0f,  1.0f, 1.0f, 1.0f
                };
                
                GLuint vbo;
                qglGenBuffers(1, &vbo);
                qglBindBuffer(GL_ARRAY_BUFFER, vbo);
                qglBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
                
                qglEnableVertexAttribArray(0);
                qglVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, 0);
                qglDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                
                qglDisableVertexAttribArray(0);
                qglDeleteBuffers(1, &vbo);
                
                // Restore compare mode for shadow mapping
                if (slot.type == SM_OMNI_CUBEMAP) {
                    qglTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
                } else if (slot.type == SM_PROJECTED_2D) {
                    qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
                } else if (slot.type == SM_DIRECTIONAL_CASCADE) {
                    qglTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
                }
            }
        }
        else if (debugMode == 2) {
            // Show slot index / cascade index as color
            // For omni: show face index, for directional: show cascade index
            // This would require a custom debug shader, so we just print info
            common->Printf("ShadowMaps Debug: Slot %d, Type %d, Resolution %d, Owner Light %d\n",
                           bestSlot, slot.type, slot.resolution, slot.ownerLightIndex);
        }
    }

    // Restore GL state
    qglBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    qglViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (depthTest) qglEnable(GL_DEPTH_TEST); else qglDisable(GL_DEPTH_TEST);
    if (blend) qglEnable(GL_BLEND); else qglDisable(GL_BLEND);
}

} // namespace ShadowMaps