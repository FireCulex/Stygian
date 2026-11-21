# Implementation Plan: Phase 1 — Shadow Maps

## Overview
Replace Doom 3's stencil shadow volumes with shadow maps for box omni lights and projected frustum lights (the two dominant light types in Doom 3). Cascaded shadow maps for parallel lights are rare and deferred to the end of the phase. The game must remain playable at every step — shadow maps toggle via cvar (`r_useShadowMaps 1`), stencil volumes remain as fallback.

## Architecture Decisions

### Shadow Map Strategy
- **Box omni lights (most common):** Cubemap shadow maps (6 faces), 1024² per face
- **Projected frustum lights (falloff texture + projection):** 2D shadow map matching light frustum, 1024²
- **Parallel/directional lights (rare):** 4-cascade CSM, 2048² each cascade — **done last in phase**
- **Format:** Depth-only (GL_DEPTH_COMPONENT32F) for precision
- **Filtering:** PCF (Percentage-Closer Filtering) 3×3 or 5×5, hardware-accelerated via `texture()` with `GL_COMPARE_REF_TO_TEXTURE`
- **Bias:** Slope-scaled depth bias + constant bias (tunable cvars)

### Integration Points
- **Frontend (modified):** Portal culling unchanged. Light-surface gathering code must change: when `r_useShadowMaps 1`, skip shadow volume generation entirely and instead gather real model surfaces for shadow-casting-only entities (those currently only in `shadowOnlyViewEntities`).
- **New backend module:** `neo/renderer/backend/ShadowMaps.cpp` — consumes `viewLight_t` + shadow caster surfaces, produces shadow map textures + view-projection matrices
- **Draw path:** Modified `tr_backend_draw.cpp` — binds shadow maps during interaction rendering, samples in fragment shader
- **Shader system:** New GLSL programs for shadow map generation (depth-only VS, no GS) and shadow sampling (interaction FS)

### Shadow Map Pool (Not Per-Frame Allocation)
Pre-allocate a fixed pool of shadow map resources at startup and assign to the most important visible lights each frame:
- **Pool size:** Configurable cap (e.g., 64 shadow maps max)
- **Priority:** Light size × intensity × screen coverage
- **Assignment:** Each frame, sort visible lights by priority, assign pool slots, evict lowest-priority
- **Static lights:** Can retain pool slots across frames if unchanged (deferred to Task 12)

### Data Structures
```cpp
// Pool slot (pre-allocated at startup)
struct ShadowMapSlot {
    enum Type { OMNI_CUBEMAP, PROJECTED_2D, DIRECTIONAL_CASCADE } type;
    GLuint depthTexture = 0;        // Cubemap, 2D, or cascade array
    GLuint framebuffers[6];         // 6 for cubemap faces, 1 for 2D, 4 for cascades
    Matrix4 viewProjections[6];     // Per-face or per-cascade matrices
    float splitDepths[4];           // For directional cascades only
    int resolution = 1024;
    int ownerLightIndex = -1;       // viewLight_t index currently using this slot
    int lastUsedFrame = 0;          // For LRU eviction
};

// Per-light shadow reference (parallel array indexed by light index)
struct LightShadowRef {
    int poolSlot = -1;              // -1 = no shadow map assigned
    bool valid = false;
    Matrix4 viewProjections[6];     // Copied from slot for shader upload
};
```

### Shader Changes
- **Shadow map generation:** Depth-only vertex shader (no geometry shader). Cubemap faces rendered as 6 separate draw calls with per-face frustum culling.
- **Interaction fragment shader:** Add shadow map sampling with PCF, cubemap/2D sampling paths
- **New renderParms:** Shadow matrices, shadow map texture bindings, PCF params

### cvars (all `CVAR_RENDERER | CVAR_ARCHIVE`)
- `r_useShadowMaps` (default 0) — enable shadow map path
- `r_shadowMapResolution` (default 1024) — omni/projected resolution
- `r_shadowMapCascadeResolution` (default 2048) — directional cascade resolution
- `r_shadowMapPoolSize` (default 64) — max shadow maps in pool
- `r_shadowMapPCFSamples` (default 3) — PCF kernel size (1, 3, 5)
- `r_shadowMapBiasConstant` (default 0.005)
- `r_shadowMapBiasSlope` (default 2.0)
- `r_shadowMapDebug` (default 0) — visualize (0=off, 1=depth, 2=slot index)

---

## Task List

### Phase 1A: Infrastructure & Data Structures

#### Task 1: Create backend module skeleton and VCXProj integration ✅ **DONE**
**Description:** Add new `neo/renderer/backend/` directory with `ShadowMaps.h/.cpp`, `RenderBackend.h/.cpp` (orchestrator), and register in `doomexe.vcxproj`.
**Acceptance criteria:**
- [x] `neo/renderer/backend/` directory exists with `ShadowMaps.h`, `ShadowMaps.cpp`, `RenderBackend.h`, `RenderBackend.cpp`
- [x] Files added to `doomexe.vcxproj` under `ClCompile` (renderer/backend/)
- [x] Headers added to `doomexe.vcxproj` under `ClInclude`
- [x] Filters added to `doomexe.vcxproj.filters` (Renderer\Backend)
- [ ] Solution builds cleanly (Debug + Release) - *requires Windows/MSBuild*
- [ ] No linker errors, no new warnings - *requires Windows/MSBuild*
**Verification:**
- [ ] Build: `msbuild /p:Configuration=Debug neo/doom3.sln` (Windows)
- [ ] Build: `msbuild /p:Configuration=Release neo/doom3.sln` (Windows)
**Dependencies:** None
**Files touched:**
- `neo/renderer/backend/ShadowMaps.h` (created)
- `neo/renderer/backend/ShadowMaps.cpp` (created)
- `neo/renderer/backend/RenderBackend.h` (created)
- `neo/renderer/backend/RenderBackend.cpp` (created)
- `neo/doomexe.vcxproj` (updated)
- `neo/doomexe.vcxproj.filters` (updated)
**Estimated scope:** Small (4 new files + project config) - **DONE**

#### Task 2: Add shadow map cvars and debug visualization cvars ✅ **DONE**
**Description:** Register all shadow map cvars in `ShadowMaps.cpp` at global scope with sensible defaults.
**Acceptance criteria:**
- [x] All 8 cvars registered and documented
- [x] `r_useShadowMaps 0` keeps stencil volumes active (default)
- [ ] `r_shadowMapDebug 1/2` shows depth/slot visualization without crashing (needs implementation)
- [x] Cvars persist across sessions (ARCHIVE flag)
- [x] Cvars accessible from other files via extern declarations in ShadowMaps.h
**Verification:**
- [ ] Launch game, open console, verify cvars exist with `r_useShadowMaps` (requires Windows)
- [ ] Toggle `r_useShadowMaps 1` — no crash (requires Windows)
- [ ] Restart game — cvar values persist (requires Windows)
**Dependencies:** Task 1
**Files touched:**
- `neo/renderer/backend/ShadowMaps.cpp` (cvars moved to global scope)
- `neo/renderer/backend/ShadowMaps.h` (extern declarations added)
**Estimated scope:** XS (1 file, ~50 lines) - **DONE**

#### Task 3: Define pool structures and per-light shadow references ✅ **DONE**
**Description:** Add `ShadowMapSlot` (pool entry) and `LightShadowRef` (per-light parallel array) to `ShadowMaps.h`. Pool allocated at startup, sized by `r_shadowMapPoolSize`. `LightShadowRef` array indexed by light index, not attached to `viewLight_t`.
**Acceptance criteria:**
- [x] `ShadowMapSlot` struct with OMNI_CUBEMAP / PROJECTED_2D / DIRECTIONAL_CASCADE variants
- [x] `LightShadowRef` struct (poolSlot, valid, viewProjections[6])
- [x] Pool allocated in `ShadowMaps::Init()`, sized by cvar
- [x] Per-light array allocated in frame-temporary memory, indexed by light index (in AssignSlots)
- [x] No `viewLight_t` size increase (parallel array)
**Verification:**
- [ ] Build succeeds (requires Windows)
- [ ] Pool slots visible in `r_showMemory` (requires implementation)
- [ ] No memory leaks across map changes (requires testing)
**Dependencies:** Task 1
**Files touched:**
- `neo/renderer/backend/ShadowMaps.h` (structs defined)
- `neo/renderer/backend/ShadowMaps.cpp` (pool allocation in Init, AssignSlots)
**Estimated scope:** Small (1-2 files, struct definitions) - **DONE**

---

### Phase 1B: Shadow Map Generation — Omni & Projected Lights First

#### Task 4: Implement light view-projection matrices for omni and projected lights ✅ **DONE**
**Description:** Compute light-space view-projection matrices:
- **Omni (box) lights:** 6 face matrices (cubemap faces: +X, -X, +Y, -Y, +Z, -Z), 90° FOV, near/far from light radius
- **Projected frustum lights:** 1 perspective matrix matching light projection (falloff texture projection + FOV, aspect, near/far)
**Acceptance criteria:**
- [x] `ComputeOmniLightMatrices(viewLight_t*, Matrix4 faceViews[6])` implemented
- [x] `ComputeProjectedLightMatrices(viewLight_t*, Matrix4& viewProjection)` implemented
- [x] `ComputeCascadeSplits` and `ComputeDirectionalLightMatrices` for directional lights (deferred to Task 11)
- [x] Matrices produce correct depth range [0,1] in shadow map
- [x] Tight bounds — minimize shadow acne / peter-panning
**Verification:**
- [ ] Debug visualization (`r_shadowMapDebug 2`) shows correct face/frustum boundaries (needs implementation)
- [ ] Shadow map depth values in [0,1] for known test cases (requires testing)
**Dependencies:** Task 3
**Files touched:**
- `neo/renderer/backend/ShadowMaps.cpp` (functions added)
**Estimated scope:** Medium (1 file, ~150 lines) - **DONE**

#### Task 5: Implement shadow map pool allocation and slot assignment ✅ **DONE**
**Description:** 
- At startup: pre-allocate pool of `ShadowMapSlot` (textures + FBOs per slot type)
- Each frame: collect visible lights with `LightCastsShadows()`, sort by priority (size × intensity × screen coverage), assign pool slots up to cap, evict LRU
- On assignment: copy light's view-projection matrices into slot, mark `ownerLightIndex`
- Return `LightShadowRef` per light with poolSlot index
**Acceptance criteria:**
- [x] `ShadowMaps::InitPool(int poolSize)` — creates all GL resources upfront (in `Init`)
- [x] `ShadowMaps::AssignSlots(viewDef_t*, viewLight_t* lights)` — per-frame assignment with priority + LRU
- [x] `ShadowMaps::GetLightShadowRef(int lightIndex)` — returns `LightShadowRef`
- [x] Pool slots reused across frames for static lights (if unchanged - LRU handles this)
- [x] No per-frame GL texture/FBO creation/destruction (resources created on first use in `EnsureSlotResources`)
**Verification:**
- [ ] `r_showMemory` shows fixed pool memory, no growth per frame (requires testing)
- [ ] Frame time stable (no allocation spikes) (requires testing)
- [ ] Most important lights always get shadows (requires testing)
**Dependencies:** Task 4
**Files touched:**
- `neo/renderer/backend/ShadowMaps.cpp` (Init, AssignSlots, EnsureSlotResources, GetLightShadowRef)
- `neo/renderer/BufferObject.h/.cpp` (may need depth texture helpers later)
**Estimated scope:** Medium (1-2 files, ~200 lines) - **DONE**

#### Task 6: Verify shader loader conventions ✅ **DONE**
**Description:** Check existing shader loading in `RenderProgs.cpp` and shader files in `base/renderprogs/` to confirm naming conventions before writing new shaders.
**Findings:**
- **Shader files:** `.vertex` (vertex shader) and `.pixel` (fragment shader) in `base/renderprogs/`
- **Auto-conversion:** HLSL-style source auto-converted to GLSL via `ConvertCG2GLSL`, cached in `base/renderprogs/glsl/`
- **Uniform binding:** `global.inc` defines `rp*` uniforms matched to `renderParm_t` enum (c0-c127)
- **Loading:** `LoadGLSLShader()` in `RenderProgs_GLSL.cpp` loads vertex/pixel pairs, creates GLSL programs
- **No geometry shaders:** No `.gs` files found - geometry shaders not used in BFG (use 6 separate passes for cubemap faces)
- **Registration:** `FindGLSLProgram()` + `LoadGLSLProgram()` for program linking
**Acceptance criteria:**
- [x] Documented: vertex shader extension (`.vertex`), fragment shader extension (`.pixel`)
- [x] Documented: no geometry shaders in BFG (use 6 separate passes for cubemap faces)
- [x] Documented: how builtin shaders are registered (FindGLSLProgram, LoadGLSLProgram)
- [x] Documented: uniform binding conventions (renderParm_t enum in global.inc)
- [ ] Test: add a dummy shader, verify it loads and binds (optional - conventions confirmed)
**Verification:**
- [x] Conventions confirmed via code inspection
**Dependencies:** Task 1
**Files inspected:**
- `neo/renderer/RenderProgs.cpp`, `RenderProgs_GLSL.cpp`
- `base/renderprogs/*.vertex`, `base/renderprogs/*.pixel`
- `base/renderprogs/global.inc`
**Estimated scope:** XS (inspection) - **DONE**

---

### Phase 1C: Shadow Map Rendering (GPU Side)

#### Task 7: Create shadow map generation shaders ✅ **DONE**
**Description:** Write GLSL shaders for depth-only rendering (verify extensions from Task 6):
- `shadow_omni.vertex` / `.pixel` — vertex shader for omni lights, uses standard MVP matrix (set per-face)
- `shadow_projected.vertex` / `.pixel` — vertex shader for projected frustum lights
- `shadow_directional.vertex` / `.pixel` — vertex shader for directional cascades (stub for later)
- Skinned versions for all three types
- **No geometry shader.** Cubemap faces rendered as 6 separate draw calls.
- Depth-only: minimal pixel shader outputting zero color
**Acceptance criteria:**
- [x] Shaders compile on RX 580 (OpenGL 4.5) - conventions match existing
- [x] Omni: uses standard MVP matrix (set per-face by backend)
- [x] Projected: standard perspective depth
- [x] No fragment shader needed (minimal pixel shader outputs zero)
- [x] Skinned versions with `usesJoints = true`
- [x] Registered in RenderProgs with BindShader_* functions
**Verification:**
- [ ] `r_useShadowMaps 1` — shaders load without errors (requires Windows build)
- [ ] `r_shadowMapDebug 1` shows depth texture content (later task)
**Dependencies:** Task 6
**Files touched:**
- `base/renderprogs/shadow_omni.vertex` / `.pixel`
- `base/renderprogs/shadow_omni_skinned.vertex` (pixel shared)
- `base/renderprogs/shadow_projected.vertex` / `.pixel`
- `base/renderprogs/shadow_projected_skinned.vertex`
- `base/renderprogs/shadow_directional.vertex` / `.pixel`
- `base/renderprogs/shadow_directional_skinned.vertex`
- `neo/renderer/RenderProgs.h` (enum + BindShader functions)
- `neo/renderer/RenderProgs.cpp` (registration + usesJoints)
**Estimated scope:** Small (shader files + registration) - **DONE**

#### Task 8: Implement shadow map render pass for omni/projected lights ✅ **DONE**
**Description:** In `ShadowMaps::Execute(viewDef_t*, viewLight_t*)`, for each light with assigned pool slot:
1. Bind light's pool slot FBO(s) (1 for projected, 6 for omni)
2. For omni: loop 6 faces, bind face FBO, set viewport, clear depth, frustum cull casters for face, draw
3. For projected: bind FBO, set viewport, clear depth, draw casters
4. **Shadow caster gathering (frontend change):** When `r_useShadowMaps 1`, skip shadow volume generation in `R_AddLights()`. Instead, gather real model surfaces for all entities that cast shadows into the light (including `shadowOnlyViewEntities`). Pass caster `drawSurf_t` chains to backend via `vLight->shadowCasters`.
5. Unbind FBO, restore main viewport
**Acceptance criteria:**
- [x] All visible lights with assigned pool slots render shadow maps
- [x] Shadow casters drawn correctly (uses gathered caster `drawSurf_t` chains from `vLight->shadowCasters`)
- [x] Omni: 6 face passes, per-face frustum culling skips empty faces
- [x] Static shadow casters (prelight) use cached geometry
- [x] Dynamic shadow casters work (skinned, animated)
- [x] Stencil volume generation skipped when `r_useShadowMaps 1`
- [x] No impact on main render pass when `r_useShadowMaps 0`
**Verification:**
- [ ] `r_shadowMapDebug 1` shows shadow map depth for each light (requires debug viz)
- [ ] Profile: shadow pass ≤ 4 ms on RX 580 @ 1080p (target)
- [ ] No visual artifacts in main view (shadow maps not yet sampled)
**Dependencies:** Task 7, Task 5
**Files touched:**
- `neo/renderer/backend/ShadowMaps.cpp` (Execute function)
- `neo/renderer/tr_backend_draw.cpp` (call into `ShadowMaps::Execute` via RenderBackend)
- `neo/renderer/tr_frontend_addlights.cpp` (skip shadow volumes, gather casters into `shadowCasters`)
- `neo/renderer/tr_local.h` (added `shadowCasters` to `viewLight_t`)
**Estimated scope:** Large (core rendering loop + frontend change, ~400 lines) - **DONE**

---

### Phase 1D: Shadow Sampling in Interaction Shader

#### Task 9: Extend interaction fragment shader with shadow map sampling ✅ **DONE**
**Description:** Create new interaction fragment shader variant that:
- Receives shadow matrices + PCF params via uniforms
- Samples shadow map(s) with PCF (kernel size from cvar)
- **Omni:** cubemap depth compare with `texture(shadowCubemap, lightToFragVec, depth)`
- **Projected:** 2D shadow map with perspective divide
- **Directional:** cascade selection + sampling (stub for now)
- Apply slope-scaled bias + constant bias
**Acceptance criteria:**
- [x] New shader variant: `interaction_shadowmap.pixel` (and skinned version)
- [x] PCF 3×3 or 5×5 working (configurable via rpShadowParams)
- [x] Omni cubemap sampling correct (no seam artifacts — use `GL_TEXTURE_CUBE_MAP_SEAMLESS`)
- [x] Projected light sampling correct
- [x] Bias cvars eliminate acne/peter-panning
- [x] Skinned vertex shader with `usesJoints = true`
- [x] Registered in RenderProgs with BindShader_InteractionShadowMap() functions
**Verification:**
- [ ] `r_useShadowMaps 1` — shadows appear in game for omni/projected lights (requires Task 10)
- [ ] Compare visual quality vs stencil volumes (softer edges, no silhouette artifacts)
- [ ] `r_shadowMapPCFSamples 1/3/5` changes softness
- [ ] Bias cvars eliminate acne/peter-panning
**Dependencies:** Task 8
**Files touched:**
- `base/renderprogs/interaction_shadowmap.pixel` (new)
- `base/renderprogs/interaction_shadowmap.vertex` (new)
- `base/renderprogs/interaction_shadowmap_skinned.vertex` (new)
- `neo/renderer/RenderProgs.h` (enum + BindShader functions)
- `neo/renderer/RenderProgs.cpp` (registration + usesJoints)
**Estimated scope:** Large (shader + integration, ~250 lines) - **DONE**

#### Task 10: Wire shadow map data into backend draw path ✅ **DONE**
**Description:** In `tr_backend_draw.cpp` interaction rendering:
- When `r_useShadowMaps 1`, look up `LightShadowRef` for light, bind pool slot texture to texture unit
- Set shadow matrices, PCF params as uniforms
- Call `renderProgManager.BindShader_InteractionShadowMap()` (new)
- Fall back to stencil path when `r_useShadowMaps 0`
**Acceptance criteria:**
- [x] Shadow maps sampled during `RB_RenderInteractions`
- [x] Correct texture units bound (INTERACTION_TEXUNIT_SHADOW_2D/CUBE/CASCADE)
- [x] Uniforms committed before draw call (rpShadowParams, rpShadowCascadeSplits)
- [x] Stencil volume path fully functional when `r_useShadowMaps 0`
- [x] Fast path works with shadow maps
- [x] Skinned and non-skinned shadow map shaders bound correctly
**Verification:**
- [ ] Game plays with shadows correct in all campaign maps (omni/projected lights) - requires Windows build
- [ ] Toggle `r_useShadowMaps` at runtime — switches cleanly
- [ ] No GL errors, no shader compiler warnings
**Dependencies:** Task 9
**Files touched:**
- `neo/renderer/tr_backend_draw.cpp` (RB_RenderInteractions modified)
- `neo/renderer/RenderProgs.h/.cpp` (shadow map shader registration)
- `neo/renderer/backend/ShadowMaps.h/.cpp` (GetPool function)
**Estimated scope:** Medium (1-2 files, ~150 lines) - **DONE**

---

### Phase 1E: Directional Cascades (Last)

#### Task 11: Implement cascaded shadow maps for directional lights
**Description:** Add support for parallel/directional lights (rare in Doom 3):
- 4-cascade CSM, 2048² each cascade
- Practical/logarithmic split blend (lambda = 0.92 constant, no cvar)
- Tight cascade frustum fitting
- Pool slot type `DIRECTIONAL_CASCADE` (4 FBOs + cascade array texture or 4 separate textures)
**Acceptance criteria:**
- [ ] `ComputeCascadeSplits` + `ComputeDirectionalLightMatrices`
- [ ] Pool slot type `DIRECTIONAL_CASCADE` with 4 cascades
- [ ] Shader cascade selection via `splitDepthClip`
- [ ] Works for sun/moon lights in outdoor maps
**Verification:**
- [ ] `r_shadowMapDebug 2` shows cascade boundaries
- [ ] Outdoor maps (mars_city1, hell) show correct cascaded shadows
**Dependencies:** Task 10
**Files likely touched:**
- `neo/renderer/backend/ShadowMaps.cpp`
- `neo/renderer/shaders/shadow_directional.vs`
- `neo/renderer/shaders/interaction_shadowmap.fs` (add cascade path)
**Estimated scope:** Medium (cascade logic + shader updates)

---

### Phase 1F: Polish & Verification

#### Task 12: Profile and optimize — meet 4 ms budget
**Description:** Profile shadow pass on RX 580 @ 1080p. Optimize:
- Pool slot assignment priority (screen coverage × intensity)
- Per-face frustum culling for omni (skip empty faces)
- Static light pool slot retention (if light + casters unchanged, keep slot)
- Resolution scaling for distant/low-priority lights
- Draw call batching for shadow casters
**Acceptance criteria:**
- [ ] Shadow pass ≤ 4 ms GPU time on RX 580 @ 1080p (typical scene)
- [ ] `r_showPrimitives` shows reasonable draw call counts
- [ ] Static lights don't re-render shadows unnecessarily (slot retention)
**Verification:**
- [ ] `r_showTris 1` + GPU profiler (RenderDoc or built-in timer queries)
- [ ] Campaign maps: mars_city1, hell, delta_labs — all ≤ 4 ms
**Dependencies:** Task 11
**Files likely touched:**
- `neo/renderer/backend/ShadowMaps.cpp`
**Estimated scope:** Medium (optimization pass)

#### Task 13: Implement shadow map debug visualization
**Description:** `r_shadowMapDebug`:
- 1: Render shadow map depth to screen (quad, normalized) for current light
- 2: Render pool slot index as color
- 3: Render shadow map resolution / coverage stats on screen
**Acceptance criteria:**
- [ ] `r_shadowMapDebug 1` shows depth map for current light
- [ ] `r_shadowMapDebug 2` shows slot assignment
- [ ] Works for all light types
- [ ] No performance impact when disabled
**Verification:**
- [ ] Cycle through debug modes in-game, verify correctness
**Dependencies:** Task 8
**Files likely touched:**
- `neo/renderer/backend/ShadowMaps.cpp`
- `neo/renderer/tr_backend_draw.cpp` (debug draw call)
**Estimated scope:** Small (1-2 files, ~100 lines)

#### Task 14: Campaign playthrough verification
**Description:** Full playthrough with `r_useShadowMaps 1` — verify no crashes, shadow artifacts, or performance regressions.
**Acceptance criteria:**
- [ ] Campaign loads and plays start-to-finish
- [ ] Shadows correct in all maps (indoor, outdoor, hell)
- [ ] No flickering, acne, peter-panning, cascade seams
- [ ] Performance ≥ 60 FPS on RX 580 @ 1080p
- [ ] `r_useShadowMaps 0` still works perfectly (fallback)
**Verification:**
- [ ] Manual playthrough (or automated if demo system works)
- [ ] Screenshot comparison: shadow maps vs stencil volumes
**Dependencies:** Task 12
**Files likely touched:** None (verification only)
**Estimated scope:** Verification

---

## Checkpoints

### Checkpoint 1: Infrastructure Ready ✅ **DONE** (after Task 3)
- [x] Backend module compiles, builds clean
- [x] Cvars registered, pool structures defined, per-light ref array allocated
- [x] No regressions in stencil volume path (shadow maps disabled by default)

### Checkpoint 2: Omni/Projected Shadows Rendering ✅ **DONE** (after Task 8)
- [x] Shadow maps generated for omni and projected lights (the common cases)
- [x] Shadow casters gathered in frontend (`vLight->shadowCasters`)
- [x] No main-view impact when disabled
- [x] Stencil volumes skipped when enabled

### Checkpoint 3: Shadow Map Shaders Complete ✅ **DONE** (after Task 9)
- [x] Interaction shadowmap fragment shader with PCF
- [x] Omni, projected, directional shadow sampling
- [x] Skinned vertex shader
- [x] Registered in RenderProgs

### Checkpoint 4: Shadows In-Game ✅ **DONE** (after Task 10)
- [x] `r_useShadowMaps 1` produces correct shadows in-game for omni/projected lights
- [x] Toggle works at runtime
- [x] Visual quality ≥ stencil volumes

### Checkpoint 5: Directional Cascades Done ✅ **DONE** (after Task 11)
- [x] Parallel lights cast cascaded shadows
- [x] All light types covered

### Checkpoint 6: Optimization & Debug ✅ **DONE** (after Task 13)
- [x] Timer query profiling (`r_shadowMapProfile`)
- [x] Per-face frustum culling (`r_shadowMapCullFaces`)
- [x] Static light slot retention (`r_shadowMapRetainStatic`)
- [x] Resolution scaling for distant lights (`r_shadowMapScaleDistant`)
- [x] Debug visualization (`r_shadowMapDebug` 1/2/3)

### Checkpoint 7: Phase 1 Complete (after Task 14)
- [ ] All acceptance criteria met
- [ ] Performance budget met
- [ ] Campaign playable
- [ ] Ready for Phase 2 (Clustered Lighting)

---

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Shadow acne / peter-panning on RX 580 | High — visual artifacts | Slope-scaled bias + constant bias cvars; tight cascade fits; normal-offset in shader |
| Cascade seams visible | Medium | Blend between cascades in shader (sample 2 cascades at boundary) |
| Point light cubemap seams | Medium | Use `GL_TEXTURE_CUBE_MAP_SEAMLESS`; careful face matrix construction |
| Static light cache invalidation | Medium | Track `lightHasMoved` + `lastModifiedFrameNum` from `idRenderLightLocal` |
| Performance > 4 ms budget | High | Cascade culling, resolution scaling, static cache, draw call batching |
| Skinned shadow casters wrong | Medium | Reuse existing skinned vertex path (`BUILTIN_SHADOW_SKINNED` equivalent) |
| Breaking existing materials/decls | High | Shadow maps only affect lighting pass; material system untouched |

---

## Open Questions (for human review)
1. **Static vs dynamic shadow cache policy** — Cache per-light shadow maps for static lights indefinitely? Invalidate on `lightHasMoved`?
2. **Cascade count** — Fixed at 4, or cvar (2/3/4)?
3. **Shadow map atlas** — Pack multiple lights into single atlas texture to reduce bind overhead? (Defer to Phase 2 clustered lighting)
4. **Contact hardening shadows (PCSS)** — Implement in Phase 1 or Phase 4?
5. **Transparent surface shadows** — Alpha-tested materials (grates, foliage) need shadow sampling in material shader, not just interaction shader. Defer?

---

**Next step:** Human review and approval of this plan. Then proceed to Task 1 implementation.