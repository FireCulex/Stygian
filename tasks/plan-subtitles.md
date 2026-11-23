# Implementation Plan: Subtitles (v1 — engine architecture + Mars City 2 load lines)

Rewritten 2026-11-21. Supersedes the earlier Whisper-first draft.

## Overview

- The engine gains **subtitle display infrastructure**: it reads ONE user-generated JSON file (`subtitles/subtitles.json` in the game install) keyed by sound shader name, and draws timed subtitles as a 2D overlay. No transcriptions ever ship in the repo (dialogue audio is copyrighted) — a separate user-run generator tool (Phase 2) produces the file from the user's own game install.
- **v1 scope = architecture only**, proven on two real dialogue lines in `game/mars_city2`: the level-load announcement ("All remaining units, this is Sgt. Kelly…") and the nearby "Hey you, up here!". Checkpoint 1 uses a **hand-written JSON** — no Whisper involved yet.
- **Deferred to future work (do not start without user approval):** PDA audio logs, radio chatter, AI chatter, cinematics / PDA video disks, menu toggle, full styling/accessibility options, full-campaign coverage.
- Hard constraints (per AGENTS.md / BUGS.md): engine-only changes under `neo/`; `base/` stays git-clean; no shader changes; the engine must not *depend* on loose files in `base/` — a missing subtitle JSON means silently no subtitles.

## Where settings are saved (answer to open question)

- Archived CVars are persisted by `idCommonLocal::WriteConfiguration()` → `WriteConfigToFile( CONFIG_FILE )` (`neo/framework/Common.cpp:348-375`), with `CONFIG_FILE` = `"D3BFGConfig.cfg"` (`neo/framework/Licensee.h:44`), written via `fileSystem->OpenFileWrite()` whose default base is `"fs_savepath"` (`neo/framework/FileSystem.cpp:152,3002`). On this Linux port `fs_savepath` is `~/.doom3bfg` — the **game-written profile**, not the repo.
- `base/default.cfg` in the repo is stock read-only defaults and is **never touched**: CVar defaults are set in the `idCVar` constructor in code, and any `CVAR_ARCHIVE` flag auto-saves the value to `D3BFGConfig.cfg` on exit. So `set ui_subtitles 1` in the console persists across restarts with zero repo changes.

## Architecture decisions

1. **User-run generator** (Phase 2) extracts audio from the user's `_sound_pc_en.resources` / `_sound_pc.resources`, transcribes offline (Whisper), writes `base/subtitles/<sound_shader>.json` into the **game install** dir. Engine only reads. `.gitignore` guards so no output is ever committed.
2. **Storage**: `base/subtitles/<sound_shader_name>.json` under `fs_basepath` (the Steam install), loaded on demand via `fileSystem->ReadFile`, LRU-cached in memory (~50 entries). Missing or malformed file → warn + return null, never crash.
    - **Implemented (2026-11-21, deviates from the per-shader sketch above):** a single combined file `subtitles/subtitles.json` (cvar `ui_subtitleFile`) holding `{ "sounds": { "<shader>": { speaker, words[] } } }`, parsed non-fatally by `neo/framework/SubtitleLoader.cpp`; lookup by shader name via `Icmp`. The generator (`tools/subtitle_gen/`) emits both the combined file and per-shader files. Malformed → warning + no subtitles, never crash.
3. **JSON schema** (matches `subTimingWordData_t` in `neo/swf/SWF_TextInstance.h:31-38` and the `addSubtitleInfo(phrase, startTime, forceBreak)` args at `SWF_TextInstance.cpp:1228-1242`; **times are ms relative to sound start**):
   ```json
   { "speaker": "Sgt. Kelly",
     "words": [ {"phrase": "All", "startTime": 0, "forceBreak": false}, ... ] }
   ```
4. **Display: engine-drawn 2D overlay, not SWF.** Verified by extracting every `.bswf` from the stock `_common.resources` / `_ordered.resources`: **none contains any "subtitle" string** — `hud.bswf`'s text fields are only `txtName`, `audioLog`, `communication`, `weaponName`, `hitTargetName`, `pdaNameDownload`, `textEntry`, and `shell.bswf` has no subtitles menu entry. The `idSWFTextInstance` subtitle API and `swf_subtitle*` CVars exist in code but are dormant with stock assets. So v1 renders with `idDeviceContext::DrawText` (`neo/ui/DeviceContext.h:58`) plus a background quad (`DrawStretchPic`), drawn **after** the HUD pass (`idMenuHandler::Update()` → `gui->Render(...)`, `neo/d3xp/menus/MenuHandler.cpp:273`). Uses existing gui renderprogs — no shader changes. Word-by-word reveal is driven by the SubtitleManager from `gameLocal.time`, ticked each frame in the HUD update path.
5. **Hook: `idEntity::StartSoundShader`** (`neo/d3xp/Entity.cpp:1643`) — the single funnel for all `StartSound` calls (`:1635`); it carries the speaking entity, the channel, and the shader name. v1 filters `SND_CHANNEL_VOICE` / `SND_CHANNEL_VOICE2` (`neo/d3xp/Game_local.h:727-745`; `SND_CHANNEL_PDA_AUDIO` / `PDA_VIDEO` / `RADIO` deferred). The enum comment requires order to stay in sync with `doom_defs.script` — we add nothing.
6. **Config**: `ui_subtitles` CVar (0=off, 1=on, 2=forced), `CVAR_ARCHIVE`, default set in code. Plus a `subtitle_reload` console command for dev iteration. No menu item in v1 (would require `shell.bswf` surgery).
7. **Timing**: record `gameLocal.time` at `StartSoundShader`; elapsed = now − start; reveal each word at its `startTime`; break lines at `forceBreak`; hide after last word + a linger (reuse the `swf_subtitleExtraTime` default of 3500 ms as our own CVar).

## Task list

### Phase 1 — architecture (v1); checkpoint = the two mars_city2 lines

#### Task 1: Subtitle loader
- `neo/framework/SubtitleLoader.cpp/h`: `LoadSubtitle( soundShaderName )` → parsed struct or null; `idParser`-based; LRU cache ~50; malformed JSON → warning + null (input is user-generated, must never crash).
- Acceptance: unit-testable parse of a sample JSON; missing file → null silently.

#### Task 2: Subtitle manager
- `neo/framework/SubtitleManager.cpp/h` (singleton): `OnSoundStart( entity, shader, channel )`, active-subtitle state, per-frame `Tick()` (word reveal from `gameLocal.time`), `ui_subtitles` CVar, `subtitle_reload` command.
- Acceptance: console log `Subtitle: <shader> speaker=<name>` when a hooked sound starts with a JSON present.

#### Task 3: Sound hook
- In `idEntity::StartSoundShader` (`neo/d3xp/Entity.cpp:1643`): if `ui_subtitles > 0` && channel ∈ {VOICE, VOICE2} && loader finds a JSON → `SubtitleManager::Start(...)`, passing entity (for potential speaker fallback) + shader name + start time.
- Acceptance: no regression in sound playback; trigger visible in log.

#### Task 4: Overlay rendering
- Draw after the HUD render in the HUD update path (`idMenuHandler_HUD::Update` / `idMenuScreen_HUD`, `neo/d3xp/menus/MenuHandler_HUD.cpp:39`): background quad + `idDeviceContext::DrawText`, bottom-center default, `SPEAKER: ` prefix when JSON has a speaker, word reveal per timings, auto-hide after last word + linger.
- Acceptance: style constants in one place; minimal `ui_subtitleScale` CVar; full styling/accessibility deferred to Phase 3.

#### Task 5: Checkpoint 1 — the two mars_city2 lines end-to-end
- Hand-write `base/subtitles/<shader>.json` (in the **Steam install**, never the repo) for these two sounds (found in `sound/map_marscity2.sndshd` inside `_common.resources` / `_ordered.resources`):
  - Level-load announcement ("All remaining units, this is Sgt. Kelly…"): shader **`marscity2_comp_vidconnect_req`** → `sound/vo/mars_city2/com_video_request.wav`, played on `SND_CHANNEL_VOICE2` from `script/map_marscity2.script` (`$speaker_280.startSoundShader(...)` in the video-connect request loop at map start).
  - "Hey you, up here!": most likely shader **`marscity2_ceiling_trigger`** → `sound/vo/mars_city2/02_ceilingguy_trigger.wav` (siblings: `marscity2_ceiling_primary`, `marscity2_ceiling_secondary1/2` → `03/04/05_ceilingguy_*.wav`). Confirm by ear in-game; the `Subtitle: <shader>` console log from Task 3 identifies exactly which shader fired.
- JSON filenames: `base/subtitles/marscity2_comp_vidconnect_req.json` and `base/subtitles/marscity2_ceiling_trigger.json` (speaker `"Sgt. Kelly"` / ceiling marine respectively).
- Verify on Xvfb `:99` first, then user verifies on `:0`: `+map game/mars_city2` → overlay appears at level load, word reveal timed to the audio, disappears when the line ends; `set ui_subtitles 0` hides it; value persists across restart via `D3BFGConfig.cfg`. Screenshots to `/tmp/opencode/`.

### Phase 2 — generator tool (real JSONs at scale)

#### Task 6: Audio extraction (`tools/subtitle_gen/`)
- Standalone tool, source-only in repo: parse `.sndshd` decls and extract referenced `.idwav` files from `_sound_pc_en.resources` / `_sound_pc.resources` by linking/reusing engine code (`idResourceContainer::ExtractResourceFile`, `neo/framework/FileSystem.cpp:2227`; sound-shader parsing in `neo/sound/snd_shader.cpp`); convert `.idwav` → WAV via `idWaveFile` (`neo/sound/snd_wavefile.cpp`).
- Output: `extracted/<shader>.wav` + `manifest.json`.

#### Task 7: Transcription
- `transcribe.py` using `faster-whisper` or `whisper.cpp` (CPU): word-level timings, VAD silence trim, `forceBreak` on sentence ends; emits one JSON per shader in the schema above, with `speaker` taken from `idDeclAudio::GetAudioName()` (`neo/framework/DeclPDA.h:104`) where the shader is referenced by a PDA audio decl.
- Acceptance: timing accuracy spot-checked on 5 files against the audio.

#### Task 8: Packaging
- `tools/subtitle_gen/README.md` (step-by-step, targets the game install dir only), `.gitignore` for all generated output. No prebuilt binaries, no CI.

#### Task 9: all-base-game-maps runner (implemented 2026-11-23)
- `tools/subtitle_gen/generate.py` now takes **no required arguments**: `python3 generate.py` auto-detects the Steam install (libraryfolders.vdf + `$DOOM3BFG_INSTALL` + `/mnt/*/SteamLibrary` + default roots; `--install` overrides), then scans the **27 base-game maps one by one** (campaign order, `BASE_GAME_MAPS` in generate.py; `--maps a,b` subsets) and merges each map's cues into the single `<install>/base/subtitles/subtitles.json`. Whisper responses are cached in `extracted/_cache/` **shared across maps** (`transcribe.py --cache-dir`), so shaders used by several maps are transcribed once. Speaker maps: every `speakers*.json` next to the script is merged into `extracted/speakers_all.json` automatically.
- **Map → vo-dir discovery (do not re-derive):** map names do NOT match `sound/vo/` dir names. `extract_level.py --map <map>` discovers a map's vo dirs at runtime via: (a) vo dir equal to the map name or equal after stripping non-alphanumerics (`mars_city1`→`marscity`, `alphalabs4`→`alpha_labs4`, `alphalabs2`→`alpha_labs2`); (b) raw `"s_shader" "sound/vo/..."` values in `maps/game/<map>.map`; (c) decls in `sound/<map>.sndshd` / `sound/map_<map>.sndshd` (name match after stripping `sound/`,`.sndshd`, leading `map`, non-alphanumerics — equality, NOT substring, so `enpro` does not pull expansion dirs `enpro1`/`enpro2`). Cross-referenced map dirs (e.g. `enpro`→`alpha_labs1`, `delta2a`→`hell`, `caverns1`→`caverns2`) are included on purpose — those lines play in that map.
- **Excluded (later phases, denylist `GENERIC_VO_DIRS` in extract_level.py):** `audio_logs` (PDA logs), `video`, `combat_chatter`, `taunts`, `feedback`, `_generic`, `ctf`, `end`, `exis`, `hell_post`, `underground`, and all RoE dirs (`erebus1-6`, `phobos1-4`, `enpro1/2`). Base game only per user 2026-11-23.
- Only `sound/map_*.sndshd` exist for some maps (alphalabs4, comm1, cpu, cpuboss, ending, erebus*, hellhole, marscity2, phobos*, site3) and several reference no vo at all — hence rules (a)/(b) carry most of the weight.
- Verified end-to-end 2026-11-23: `generate.py --maps hellhole` → 2 cues transcribed via local whisper-server (tiny.en on :8081), merged into install `subtitles.json` (17 existing mars_city2 entries preserved → 19). Full 27-map run launched in background, log `/tmp/opencode/subgen_all.log`.

### Phase 3 — deferred future work (explicitly out of scope until approved)
- PDA audio logs (`SND_CHANNEL_PDA_AUDIO`, `MenuHandler_PDA.cpp`), radio chatter (`SND_CHANNEL_RADIO`, `Misc.cpp:3111`), AI/anim chatter (`idAI`, `Anim_Blend.cpp`), PDA video disks & cinematics (`SND_CHANNEL_PDA_VIDEO` — note cinematic audio is currently silent on this port, see B8 in BUGS.md), menu toggle (needs `shell.bswf`), full styling + WCAG 2.2 AA contrast options, overlapping-dialogue priority, full-campaign playthrough test.

## Risks and mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Copyright — shipping transcriptions | Blocker | Zero transcriptions in repo; generator runs on user's machine only; `.gitignore` on outputs |
| Malformed user JSON crashes game | High | Parser warns + returns null; fuzz a few bad files in the unit test |
| Timing drift between sound start and overlay | Medium | Times relative to `StartSoundShader`; validate on the two mars_city2 lines on `:0` |
| Wrong shader hooked (VOICE picks up non-dialogue) | Low | Log every trigger; v1 limited to VOICE/VOICE2; other channels deferred |
| Performance | Low | LRU cache, O(1) lookup, <1 ms/frame |
| Repo `base/` contamination | High | Nothing in repo is read or written for this; generator writes only under the game install's `base/subtitles/` |
| Whisper accuracy (Phase 2) | Medium | Manual review/regeneration; `forceBreak` hints |

## Open questions — resolved during review (2026-11-21)

1. **SWF subtitle field?** None exists in any stock `.bswf` (verified by extraction) → engine-drawn overlay chosen; SWF injection is the documented fallback.
2. **Speaker names?** Available at generation time via `idDeclAudio::audioName`; embedded as `speaker` in the JSON.
3. **Video-disk subtitles?** Deferred — cinematic audio is silent on this port (B8).
4. **`subtitle_reload` console command?** Yes, included (Task 2).
5. **`swf_subtitle*` CVars functional?** They exist (`SWF_TextInstance.cpp:39-41`) but nothing in stock assets drives them → not used in v1.
6. **Generator distribution?** Source-only under `tools/subtitle_gen/`; no prebuilt binary.

## Verification loop (per BUGS.md working agreements)

- Build: `cmake --build neo/build_linux --target doom3bfg`
- Copy the exe into the Steam dir **only after the game has fully exited**; smoke-test headless on Xvfb `:99`; user verifies on `:0` with the window visible and **hard mute** (`+set s_noSound 1 +set s_volume_dB -100`):
  `cd "<steam dir>" && DISPLAY=:0 ./doom3bfg +set r_fullscreen 0 +set r_windowX 40 +set r_windowY 40 +set r_windowWidth 1280 +set r_windowHeight 720 +set com_skipIntroVideos 1 +set s_noSound 1 +set s_volume_dB -100 +map game/mars_city2`
- Checkpoint 1 acceptance: on `mars_city2` load the Kelly line and "Hey you, up here!" appear as timed overlays at the right moments; `Subtitle:` lines in the console log; `set ui_subtitles 0` disables; setting persists across restart; screenshots/log to `/tmp/opencode/`.
