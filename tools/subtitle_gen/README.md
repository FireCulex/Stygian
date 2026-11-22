# subtitle_gen — user-run subtitle JSON generator for Doom 3 BFG

Generates `base/subtitles/<shader>.json` files **in the game install** from the
install's own audio, transcribed by a locally running whisper.cpp server.
Nothing generated is ever committed to this repo (see `.gitignore`): dialogue
audio and transcriptions are copyrighted and stay on the user's machine.

The engine only *reads* these JSONs (loader in `neo/framework/SubtitleLoader`);
a missing file silently means no subtitles.

## Requirements

- Python 3.8+, stdlib only (no pip installs).
- A running [whisper.cpp](https://github.com/ggerganov/whisper.cpp) server with
  token timestamps enabled, e.g.:

  ```
  whisper-server -m /path/to/ggml-base.en.bin --host 0.0.0.0 --port 8081
  ```

  Word timings are requested via `token_timestamps=true` + `verbose_json`.
- The game install dir (the one containing `base/`), e.g. the Steam
  `DOOM 3 BFG Edition` folder.

## Usage

```sh
cd tools/subtitle_gen
python3 generate.py \
    --install "/mnt/wd_mybook/SteamLibrary/steamapps/common/DOOM 3 BFG Edition" \
    --level mars_city2 \
    --server http://127.0.0.1:8081 \
    --speakers speakers_marscity2.json
```

Steps performed:

1. `extract_level.py` scans every `.resources` pack under `<install>/base/`,
   parses all `.sndshd` sound-shader decls plus raw `"s_shader" "sound/..."`
   values in `maps/game/<level>.map`, keeps everything under
   `sound/vo/<level>/`, and re-wraps each `generated/*.idwav` as a playable
   RIFF/WAVE (`idwav2wav.py`). Output: `extracted/<level>/*.wav` +
   `manifest.json`.
2. `transcribe.py` POSTs each WAV to `<server>/inference`
   (`response_format=verbose_json`, `token_timestamps=true`, `language=en`,
   `temperature=0.0`), converts the word timestamps (seconds -> int ms
   relative to sound start), and groups words into **line-level cues** at
   sentence/segment boundaries (whisper's `forceBreak` markers: words ending
   in `. ! ? …` and the last word of each segment). Cue text is the words
   smart-joined (punctuation tokens like `,` / `.` re-attached, so no stray
   spaces and no `"reg" + "roup"` splits; when a cue covers exactly one
   whisper segment, the segment's own text is used). It writes **only**:
   - `<install>/base/subtitles/subtitles.json` — the **combined file the
     engine actually loads** (`ui_subtitleFile` cvar, default
     `subtitles/subtitles.json` under the game dir):

     ```json
     { "version": 2, "generator": "tools/subtitle_gen", "language": "en",
       "sounds": {
         "mc2_sarge_radio1": {
           "speaker": "Sgt. Kelly",
           "cues": [
             { "start": 120,  "end": 2540, "text": "All remaining marine units, this is Sergeant Kelly." },
             { "start": 2860, "end": 5780, "text": "I am unable to reach Mars City from my current position." }
           ]
         }
       } }
     ```

     Re-runs merge into the existing file (entries for shaders re-processed
     in the run are updated or removed), so `--only` filters don't clobber
     other entries. `--per-shader` additionally writes one
     `<shader>.json` per shader as a review artifact (the engine never
     reads those).

   Raw server responses are cached as `extracted/<level>/<shader>.whisper.json`
   and reused on re-runs (`--no-cache` forces re-transcription) — **always
   spot-check these before playing** (Whisper hallucinates on game audio;
   the default tiny.en model especially — use base.en or larger for final
   output). Transcripts that are pure `[ ... ]` non-speech noise are skipped.

## Naming rule (engine contract)

- In the combined `subtitles.json`, keys are the **raw shader names** exactly
  as the game passes them to `idEntity::StartSoundShader` (e.g.
  `marscity2_ceiling_trigger`, or the literal path
  `sound/vo/mars_city2/greg_discussion.wav` for entities whose `s_shader` is
  a raw wav). The loader matches with `idStr::Icmp` (case-insensitive), so
  key case does not matter.
- The per-shader `<shader>.json` files (only with `--per-shader`) sanitize
  the name (`/` and `\` -> `_`) for filesystem safety; they are
  intermediates/review artifacts.

## Files

- `idwav2wav.py` — `.resources` pack reader + `.idwav` -> RIFF/WAVE converter.
- `d3pack.py` — light table-only pack reader + `.sndshd`/`.map` parsers.
- `extract_level.py` — level WAV extraction (step 1 above).
- `transcribe.py` — whisper.cpp transcription + JSON emission (step 2 above).
- `generate.py` — runs both steps for one level.
- `speakers_marscity2.json` — example shader -> speaker map (edit as needed;
  speaker is optional in the JSON).
