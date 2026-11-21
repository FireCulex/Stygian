# Doom 3 BFG Renderer Modernization — Intent

## Outcome
Incremental renderer modernization on Doom 3 BFG:
- Phase 1: Shadow maps replacing stencil volumes
- Phase 2: Clustered/deferred lighting backend
- Phase 3: PBR material pipeline (roughness/metalness textures)
- Phase 4: SSAO/GTAO + reflection probes
- Phase 5: Volumetric fog and light shafts
- Phase 6: TAA/SMAA + post-processing

All playable through the campaign on an RX 580.

## User
Solo developer — learning project, portfolio piece, and playable mod.

## Why Now
Want a modern renderer without forking RBDOOM. The Oblivion remaster demonstrated what's achievable on mid-tier hardware (deferred PBR, volumetric lighting, modern tone-mapping).

## Success Criteria
- Campaign runs start-to-finish with shadow maps → PBR → volumetric lighting visible
- 60+ FPS on RX 580 at 1080p
- Game remains playable at every phase (incremental, no clean break)

## Constraints
- **Hardware:** RX 580 — no ray tracing, no HDR output, compute shaders available
- **Architecture:** Incremental replacement only — keep frontend intact (portal culling, interaction system, material/decl parsing, entity/animation code)
- **Materials:** Extend decl system with optional PBR keywords (roughness, metalness, packed texture stage); fall back to procedural conversion:
  - Albedo from diffuse (de-lighted)
  - Roughness from specular map
  - Metalness via per-material flag (not pixel inference)
- **Scope:** Single-player campaign only

## Out of Scope
- Ray tracing
- HDR output / HDR display support
- Full asset re-authoring (procedural conversion + selective overrides only)
- Replacing the frontend (portal culling, interaction system, decl parsing, animation)
- Multiplayer
- Clean-break renderer rewrite (weeks with nothing playable)

## Phasing Order (Fixed)
1. Shadow maps (cascaded, replacing stencil volumes — required for volumetric lighting)
2. Clustered/deferred lighting backend (lights stop being expensive)
3. PBR materials with extended decl system
4. SSAO/GTAO + reflection probes
5. Volumetric fog and light shafts (build on shadow maps)
6. TAA/SMAA + post-processing (tone-mapping, auto-exposure, bloom)