#!/usr/bin/env python3
"""
generate.py — one-shot subtitle generator for the whole Doom 3 BFG BASE GAME.

No required arguments:

    python3 generate.py            # auto-detects the Steam install, scans all
                                   # 27 base-game maps one by one, transcribes
                                   # each map's voice WAVs with a running
                                   # whisper.cpp server, and merges everything
                                   # into <install>/base/subtitles/subtitles.json

Optional:
    --install PATH     game install root (contains base/); auto-detected from
                       Steam libraryfolders.vdf (plus $DOOM3BFG_INSTALL and
                       common Steam roots) when omitted
    --server URL       whisper.cpp server (default http://127.0.0.1:8081)
    --maps a,b,c       only these maps (default: all base-game maps)
    --workdir DIR      extraction/cache workdir (default ./extracted, gitignored)
    --extract-only     extract WAVs + manifests but do not transcribe
    --no-cache         force re-transcription (ignore cached .whisper.json)
    --per-shader       also write per-shader JSON review artifacts

Outputs:
    <install>/base/subtitles/subtitles.json  (consumed by the engine via
        ui_subtitleFile; the ONLY thing written into the game install —
        never into this repo)
    <workdir>/<map>/                           (wavs, manifest.json — gitignored)
    <workdir>/_cache/                          (shared whisper responses, so
        shaders used by several maps are transcribed only once)

Expansions (Resurrection of Evil: erebus*/phobos*/le_*), PDA audio logs,
video-disk audio and generic chatter are deliberately NOT scanned — later
phases (see tasks/plan-subtitles.md).
"""

import argparse
import glob
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# The 27 base-game campaign maps (order = campaign order).
BASE_GAME_MAPS = [
    'mars_city1', 'mc_underground', 'mars_city2', 'admin',
    'alphalabs1', 'alphalabs2', 'alphalabs3', 'alphalabs4',
    'enpro', 'commoutside', 'comm1',
    'recycling1', 'recycling2', 'monorail',
    'delta1', 'delta2a', 'delta2b', 'delta3', 'delta4',
    'hell1', 'delta5', 'cpu', 'cpuboss',
    'site3', 'caverns1', 'caverns2', 'hellhole',
]

INSTALL_DIR_NAME = 'DOOM 3 BFG Edition'


def steam_library_roots():
    """steamapps dirs from every known Steam library (libraryfolders.vdf),
    plus the usual default roots and any /mnt/*/SteamLibrary mount."""
    roots = []
    for vdf in (os.path.expanduser('~/.steam/steam/steamapps/libraryfolders.vdf'),
                os.path.expanduser('~/.local/share/Steam/steamapps/libraryfolders.vdf')):
        try:
            with open(vdf) as fh:
                for m in re.finditer(r'"path"\s+"([^"]+)"', fh.read()):
                    roots.append(os.path.join(m.group(1), 'steamapps'))
        except OSError:
            pass
    roots += [os.path.expanduser('~/.steam/steam/steamapps'),
              os.path.expanduser('~/.local/share/Steam/steamapps')]
    roots += sorted(glob.glob('/mnt/*/SteamLibrary/steamapps'))
    return roots


def is_install(path):
    base = os.path.join(path, 'base')
    if not os.path.isdir(base):
        return False
    return any(f.endswith('.resources') and 'sound' in f
               for f in os.listdir(base))


def find_install(explicit=None):
    cands = []
    if explicit:
        cands.append(explicit)
    env = os.environ.get('DOOM3BFG_INSTALL')
    if env:
        cands.append(env)
    for root in steam_library_roots():
        cands.append(os.path.join(root, 'common', INSTALL_DIR_NAME))
    for c in cands:
        if is_install(c):
            return c
    sys.exit('could not auto-detect a DOOM 3 BFG install (looked in: %s)\n'
             'pass --install "/path/to/DOOM 3 BFG Edition"' % ', '.join(cands))


def run(cmd):
    print('+ ' + ' '.join(cmd), flush=True)
    return subprocess.call(cmd)


def merge_speakers(workdir, extra=None):
    """Merge every speakers*.json next to this script into one map for
    transcribe.py. Returns the path, or None when there are no speakers."""
    speakers = {}
    files = sorted(glob.glob(os.path.join(HERE, 'speakers*.json')))
    if extra:
        files.append(extra)
    for f in files:
        try:
            with open(f) as fh:
                data = json.load(fh)
            speakers.update({k: v for k, v in data.items() if not k.startswith('_')})
        except (OSError, ValueError) as e:
            print('WARN: skip speakers file %s: %s' % (f, e))
    if not speakers:
        return None
    os.makedirs(workdir, exist_ok=True)
    out = os.path.join(workdir, 'speakers_all.json')
    with open(out, 'w') as fh:
        json.dump(speakers, fh, indent=2)
    print('merged %d speaker names from %d file(s) -> %s'
          % (len(speakers), len(files), out))
    return out


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--install', default=None,
                    help='game install root (contains base/); auto-detected if omitted')
    ap.add_argument('--server', default='http://127.0.0.1:8081',
                    help='whisper.cpp server URL')
    ap.add_argument('--maps', default=None,
                    help='comma-separated subset of maps (default: all %d base-game maps)'
                         % len(BASE_GAME_MAPS))
    ap.add_argument('--workdir', default=os.path.join(HERE, 'extracted'))
    ap.add_argument('--extract-only', action='store_true',
                    help='extract WAVs + manifests but do not transcribe')
    ap.add_argument('--no-cache', action='store_true', help='force re-transcription')
    ap.add_argument('--per-shader', action='store_true',
                    help='also write per-shader JSON review artifacts')
    ap.add_argument('--speakers', default=None,
                    help='extra shader -> speaker JSON merged with speakers*.json')
    args = ap.parse_args(argv)

    install = find_install(args.install)
    base = os.path.join(install, 'base')
    out = os.path.join(base, 'subtitles')
    workdir = os.path.abspath(args.workdir)
    cache = os.path.join(workdir, '_cache')
    print('install: %s' % install)
    print('server:  %s' % args.server)
    print('workdir: %s (whisper cache: %s)' % (workdir, cache))

    want = set(args.maps.split(',')) if args.maps else None
    maps = [m for m in BASE_GAME_MAPS if not want or m in want]
    unknown = want - set(maps) if want else set()
    if unknown:
        sys.exit('unknown map(s): %s (known: %s)' % (', '.join(sorted(unknown)),
                                                      ', '.join(BASE_GAME_MAPS)))

    speakers = merge_speakers(workdir, args.speakers)

    failures = []
    for i, mp in enumerate(maps):
        print('\n=== [%d/%d] map %s ===' % (i + 1, len(maps), mp), flush=True)
        work = os.path.join(workdir, mp)
        rc = run([sys.executable, os.path.join(HERE, 'extract_level.py'),
                  '--base', base, '--map', mp, '--out', work])
        if rc != 0:
            print('EXTRACT FAILED (%d) for %s' % (rc, mp))
            failures.append((mp, 'extract'))
            continue
        if args.extract_only:
            continue
        cmd = [sys.executable, os.path.join(HERE, 'transcribe.py'),
               '--manifest', os.path.join(work, 'manifest.json'),
               '--out', out, '--server', args.server, '--cache-dir', cache]
        if speakers:
            cmd += ['--speakers', speakers]
        if args.no_cache:
            cmd += ['--no-cache']
        if args.per_shader:
            cmd += ['--per-shader']
        rc = run(cmd)
        if rc != 0:
            failures.append((mp, 'transcribe'))

    print('\n==== done: %d/%d maps ok ====' % (len(maps) - len(failures), len(maps)))
    if failures:
        print('failures: %s' % ', '.join('%s (%s)' % f for f in failures))
    print('combined subtitles: %s' % os.path.join(out, 'subtitles.json'))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
