# AGENTS.md — Doom 3 BFG (native Linux port)

Doom 3 BFG Edition GPL source with an in-progress native Linux (SDL2) port. The working tree has ~200 uncommitted modified files under `neo/` — that is the active port, do not "clean" it. **Read `BUGS.md` first**: it is the source of truth for open bugs (B1–B9) and session working agreements.

## Layout

- `neo/` — engine. Linux port lives in `neo/sys/sdl/` (SDL2 main, input, session, GL). Build with CMake, not the VS solution.
- `base/` — contains only the 91 files shipped with the GPL source release (CG renderprogs, `default.cfg`). **Never add or edit anything here**; must stay git-clean. No new shaders/materials, no loose renderprogs, ever (user policy 2026-11-20).
- `doomclassic/` — Doom 1/2, excluded from the Linux build.
- `tasks/todo.md` — Linux port + in-game verification track. `tasks/plan.md` — shadow-map renderer plan (PARKED; do not resume without user approval).
- `docs/` — renderer-modernization intent/spec. `neo/doom3.sln` — Windows-only (VS2010 + DirectX SDK), unusable on this Linux box.

## Build

```sh
cmake --build neo/build_linux --target doom3bfg
```

Build dir `neo/build_linux` is already configured (Unix Makefiles, `BUILD_FULL_GAME=ON`). Exe lands at **`neo/build_linux/doom3bfg`** (there is no `bin/`). Needs SDL2 >= 2.0.14, OpenGL, and FFmpeg libav* via pkg-config (used by the in-binary Bink decoder in `neo/renderer/Cinematic.cpp`).

## Run / test loop

- Run dir is the Steam install, symlinked as `/tmp/opencode/doom3bfg-assets`:
  `/mnt/wd_mybook/SteamLibrary/steamapps/common/DOOM 3 BFG Edition`
- Copy the exe there **only after the game has fully exited** ("Text file busy" otherwise). Never pass a path with spaces via `+set fs_basepath` (engine tokenizes on whitespace).
- Smoke-test headless on Xvfb `:99` first (survives monitor power-off; `:0` is the physical niri session, `:1` is Xwayland — not safe). User verifies on `:0`. Logs/screenshots go to `/tmp/opencode/`.
- Launcher on `:0`:
  `cd "<steam dir>" && DISPLAY=:0 ./doom3bfg +set r_fullscreen 0 +set r_windowX 40 +set r_windowY 40 +set r_windowWidth 1280 +set r_windowHeight 720 +set com_skipIntroVideos 1 +set in_useJoystick 1 +map game/mars_city2`
- **Mute every test launch**: append `+set s_noSound 1 +set s_volume_dB -100`. Leave test windows **visible** on `:0` (user watches them).
- `generateResourceCRCs` "lockup" at startup is stock behavior, not a bug — let it finish.

## Hard constraints

- **Provenance (user policy, 2026-11-23): this port is a clean-room implementation of our own — not based on other projects (no RBDOOM-3-BFG or other forks). Do not copy code from third-party projects.** Same-license (GPL) projects may be consulted as *hints only* to understand intended engine behavior (anything GPL is safe to look at license-wise), but the goal is to keep the implementation ours — never import files or code wholesale.
- Engine-only fixes: everything under `neo/`. `base/` stays stock, and the engine must not depend on loose files in `base/` — shadow-map code stays inert with stock assets (missing shadow programs → progId 0 → clean stencil fallback); no embedded/recreated shaders.
- Single-player only: `ID_SINGLEPLAYER_ONLY` plus CMake `list(REMOVE_ITEM ...)` excludes MP/lobby; Linux stubs live in `neo/sys/sdl/*_stub.cpp`.
- GPL release exclusions: no Steam integration, no Carmack's Reverse.

## Porting gotchas (LP64)

The code assumes 32-bit Windows (LLP64). Linux x86-64 crashes usually trace to pointer-size assumptions — audit nearby code when fixing one:

- `unsigned long` fields in on-disk structs (e.g. `.bcm` collision model) are 8 bytes on Linux; use fixed-width types.
- Event/script-VM arg arrays must be `intptr_t[]`, not `int[]` — pointers get truncated otherwise (`idEvent::CopyArgs`, `Script_Interpreter` data arrays, GUI `wexpOp_t` fields).
- Script VM `type_entity`/`type_object` size is `sizeof(int)`, not `sizeof(int*)`.
- `Sys_InterlockedIncrement/Decrement` must use `__atomic_add_fetch`/`__atomic_sub_fetch` — under SDL2, `SDL_AtomicIncRef` returns the OLD value.

## GL / shaders

SDL requests an OpenGL 4.5 **compat** context (Mesa grants 4.6). Renderprogs ship pre-converted GLSL (`#version 150`) embedded in `.resources`; `SanitizeGLSLForCompat()` in `neo/renderer/RenderProgs_GLSL.cpp` rewrites to `#version 140` in memory for Mesa. The engine reads shaders from the `.resources` packs, not from disk — never add a disk-cache fallback for `base/renderprogs/`.

## Current priorities (per BUGS.md)

1. B2: mars_city2 lag — repro `setpos -698.35 -2338.76 290` (console cmd exists); sample with `bash /tmp/opencode/sampler.sh` while it lags.
2. B8b: static on in-game videoMap display screens.
3. B5: flashlight overbright wedge with hard cutoff.

## Session memory (`int` project `doom-3-bfg`)

An `int` memory project named **`doom-3-bfg`** exists and is expected to be used **multiple times per session**, not just at the end:

- **Recall first**: before grep/glob/read/web-searching for a known fact, call `int.search({ project: "doom-3-bfg", query })` (`int.list` for a recency scan, `int.get` by id for full content). Prior sessions' discoveries live there.
- **Persist continuously**: call `int.add` the moment a durable fact is learned — multi-step findings, root causes, architecture/config decisions, bug fixes, negative facts ("X fails because Y"). Do not batch to end of session.
- **Compaction survival**: some bug fixes span multiple context compactions. Anything that must survive compaction goes into `int` immediately; after a compaction, re-run `int.search` instead of re-deriving from scratch. Conversation history is not durable — `int` is.
- `BUGS.md` remains the source of truth for open bugs (B1–B9) and session working agreements; `int` holds the cross-session discoveries that BUGS.md does not track.

Post a short status update on the hour when working.
