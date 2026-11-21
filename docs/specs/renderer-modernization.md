# Spec: Doom 3 BFG Renderer Modernization

## Objective
Incrementally modernize the Doom 3 BFG renderer by replacing the backend draw code while preserving the frontend (portal culling, interaction system, material/decl parsing, entity/animation). Target: playable campaign on RX 580 at 60+ FPS with modern visual fidelity (shadow maps, clustered lighting, PBR, volumetric lighting, TAA).

**User:** Solo developer — learning, portfolio, playable mod.

**Success =** Campaign runs start-to-finish with all phases visible, 60+ FPS on RX 580 at 1080p, game playable at every phase.

## Tech Stack
- **Language:** C++17 (existing codebase), GLSL 4.5 compute/fragment shaders
- **Engine:** Doom 3 BFG (idTech 4) — `neo/` folder structure
- **Graphics API:** OpenGL 4.5 / Vulkan (BFG has both backends; target OpenGL 4.5 first for RX 580 compatibility)
- **Build:** CMake (existing)
- **Platform:** Linux primary, Windows secondary

## Commands
```
Build (Debug):   cmake --build build --config Debug
Build (Release): cmake --build build --config Release
Run:             ./build/doom3bfg +set r_renderer 1
Test:            ctest --output-on-failure  (if tests exist)
Lint:            clang-format --dry-run -Werror src/renderer/
```

## Project Structure
```
neo/
├── renderer/                    ← NEW: Modern renderer backend
│   ├── backend/                 ← Draw backend (replaces tr_backend.cpp)
│   │   ├── ShadowMaps.cpp/.h    ← Phase 1: Cascaded shadow maps
│   │   ├── ClusteredLighting.cpp/.h  ← Phase 2: Light culling/clustering
│   │   ├── PBRMaterials.cpp/.h  ← Phase 3: PBR shading pipeline
│   │   ├── SSAO.cpp/.h          ← Phase 4: SSAO/GTAO
│   │   ├── Volumetric.cpp/.h    ← Phase 5: Volumetric fog/light shafts
│   │   ├── PostProcess.cpp/.h   ← Phase 6: TAA, tone-mapping, bloom
│   │   └── RenderBackend.cpp/.h ← Main backend orchestration
│   ├── frontend/                ← Kept from existing (portal culling, interactions)
│   ├── shaders/                 ← GLSL shaders (compute + graphics)
│   │   ├── shadowmaps.glsl
│   │   ├── clustered_lighting.glsl
│   │   ├── pbr_lighting.glsl
│   │   ├── ssao.glsl
│   │   ├── volumetric.glsl
│   │   └── postprocess.glsl
│   ├── resources/               ← GPU resources (buffers, textures, descriptors)
│   └── RendererModule.cpp/.h    ← Module entry point
├── framework/                   ← Existing: decl parsing, material system, entities
├── game/                        ← Existing: game logic
└── CMakeLists.txt
```

## Code Style
Follow existing idTech 4 conventions with modern C++17 additions:

```cpp
// File: neo/renderer/backend/ShadowMaps.cpp
class ShadowMapPass {
public:
    ShadowMapPass(RenderBackend& backend);
    ~ShadowMapPass();

    void Execute(const ViewDef& view, const DrawSurfList& surfaces);
    void AllocateCascades(int cascadeCount, int resolution);

private:
    struct Cascade {
        GLuint depthTexture = 0;
        GLuint framebuffer = 0;
        Matrix4 viewProjection;
        float splitDepth = 0.0f;
    };

    RenderBackend& m_Backend;
    std::vector<Cascade> m_Cascades;
    GLuint m_ShaderProgram = 0;
    bool m_Initialized = false;

    void InitShaders();
    void ComputeCascadeSplits(const ViewDef& view);
    void BindCascade(int index);
};
```

**Key conventions:**
- `m_` prefix for member variables
- PascalCase for classes/structs, camelCase for functions/variables
- RAII for GPU resources (destructor cleans up GL objects)
- `const` correctness — pass large structs by const ref
- No exceptions — return `bool` or use `Result<T>` for fallible operations
- Shaders: `#version 450 core`, explicit binding points (`layout(std430, binding = 0)`)

## Testing Strategy
- **Unit tests:** None in existing codebase — not adding new test framework (scope constraint)
- **Integration verification:** Visual regression via automated screenshot comparison (future)
- **Manual verification per phase:**
  - Phase 1: Shadow maps render correctly, no stencil volume artifacts, performance > 60 FPS
  - Phase 2: Light count scales to 100+ without FPS cliff, correct attenuation
  - Phase 3: PBR materials render with roughness/metalness, fallback conversion works
  - Phase 4: SSAO/GTAO visible in corners, reflection probes capture environment
  - Phase 5: Volumetric fog/light shafts visible through shadowed areas
  - Phase 6: TAA stable (no ghosting), tone-mapping clamps highlights, bloom thresholds work
- **Performance budgets (RX 580 @ 1080p):**
  - Shadow maps: ≤ 4 ms
  - Clustered lighting: ≤ 6 ms
  - PBR shading: ≤ 5 ms
  - SSAO: ≤ 3 ms
  - Volumetric: ≤ 4 ms
  - Post-process: ≤ 2 ms
  - **Total GPU: ≤ 24 ms (41+ FPS headroom for game logic)**

## Boundaries
- **Always:**
  - Keep game playable at every commit (incremental)
  - Follow existing CMake/GL patterns in codebase
  - Validate GL errors in debug builds (`GL_CHECK()` macro)
  - Preserve existing material decl parsing — extend only
  - Profile on RX 580 before committing phase

- **Ask First:**
  - Adding new external dependencies (libraries, tools)
  - Changing CMake minimum version or C++ standard
  - Modifying frontend systems (portal culling, interaction tables)
  - Adding new shader stages beyond compute/fragment/vertex

- **Never:**
  - Break existing material decls — fallback conversion must work
  - Delete stencil volume code until shadow maps are verified in-game
  - Commit shader compiler errors
  - Assume HDR/RT hardware features exist
  - Rewrite frontend systems (portal culling, decl parsing, animation)

## Success Criteria (Per Phase)

### Phase 1: Shadow Maps
- [ ] Cascaded shadow maps (4 cascades, 2048² each) render for directional lights
- [ ] Point/spot lights use cubemap/2D array shadow maps
- [ ] No visible shadow acne or peter-panning (bias/normal offset working)
- [ ] Stencil volume code path disabled behind cvar (`r_useShadowMaps 1`)
- [ ] Campaign loads and plays with shadows correct in all maps
- [ ] GPU time ≤ 4 ms on RX 580 @ 1080p

### Phase 2: Clustered/Deferred Lighting
- [ ] Light clustering (3D grid + light index list) on compute shader
- [ ] 100+ dynamic lights render at > 60 FPS
- [ ] Correct attenuation, cookies, projection textures
- [ ] Forward+ or deferred — decision documented in plan
- [ ] GPU time ≤ 6 ms for lighting pass

### Phase 3: PBR Materials
- [ ] Material decls support `roughness`, `metalness`, `pbrTexture` keywords
- [ ] Procedural fallback: albedo from diffuse (de-lighted), roughness from specular, metalness from flag
- [ ] GGX BRDF with energy conservation, Fresnel-Schlick
- [ ] IBL via prefiltered environment maps + BRDF LUT
- [ ] Existing materials render without modification
- [ ] GPU time ≤ 5 ms

### Phase 4: SSAO/GTAO + Reflection Probes
- [ ] GTAO (ground-truth ambient occlusion) on compute shader
- [ ] Temporal reprojection for noise reduction
- [ ] Reflection probes (box/sphere) with parallax correction
- [ ] Probe blending for smooth transitions
- [ ] GPU time ≤ 3 ms

### Phase 5: Volumetric Fog & Light Shafts
- [ ] Froxel/volume texture for fog density + lighting
- [ ] Light shafts via shadow map marching
- [ ] Temporal filter for noise
- [ ] Integrates with Phase 1 shadow maps
- [ ] GPU time ≤ 4 ms

### Phase 6: TAA + Post-Processing
- [ ] TAA with jitter, velocity buffer, history rejection
- [ ] Tone-mapping (ACES or Reinhard) + auto-exposure
- [ ] Bloom (threshold + separable blur)
- [ ] Color grading LUT support
- [ ] GPU time ≤ 2 ms

## Decisions (Resolved Open Questions)
1. **Forward+ (clustered forward)** — Not deferred. Doom 3's materials are multi-stage with lots of blended/transparent surfaces, which fit forward better. Saves bandwidth on RX 580 and keeps MSAA possible.
2. **OpenGL 4.5 only** — Original BFG is OpenGL-only. Vulkan backend came from RBDOOM (avoiding). Vulkan port would be separate project.
3. **De-lighting** — Manual overrides only at first. Add luminance-based de-lighting later if auto-converted albedo looks bad.
4. **Reflection probes** — Baked at map load, per portal area, cached to disk. Avoids hand-placing probes and per-frame capture cost.
5. **TAA velocity buffer** — Camera-only reprojection first, no per-object velocity. Skinned characters will ghost slightly. If bothersome, add separate velocity pass later rather than touching every vertex shader.

---

**Awaiting human approval before proceeding to Phase 2 (Plan).**