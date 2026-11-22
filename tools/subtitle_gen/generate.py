#!/usr/bin/env python3
"""
generate.py — one-shot subtitle generator for one level of a Doom 3 BFG
install: extract the level's voice WAVs from the .resources packs, transcribe
them with a running whisper.cpp server (word-level timestamps, grouped into
line-level cues), and write the combined <subtitles>/subtitles.json into the
game install's base/subtitles/.

Usage:
    python3 generate.py --install "/path/to/DOOM 3 BFG Edition" \
        --level mars_city2 --server http://127.0.0.1:8081 \
        [--speakers speakers_marscity2.json] [--workdir extracted]

Outputs:
    <install>/base/subtitles/subtitles.json  (consumed by the engine via
        ui_subtitleFile; the ONLY thing written into the game install by
        default — never into this repo)
    <workdir>/<level>/                       (wavs, manifest.json, raw
        *.whisper.json — gitignored, review these before trusting output)
"""

import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def run(cmd):
    print('+ ' + ' '.join(cmd))
    rc = subprocess.call(cmd)
    if rc != 0:
        sys.exit('command failed (%d): %s' % (rc, ' '.join(cmd)))


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--install', required=True, help='game install root (contains base/)')
    ap.add_argument('--level', required=True, help='level name, e.g. mars_city2')
    ap.add_argument('--server', default='http://127.0.0.1:8081')
    ap.add_argument('--speakers', default=None)
    ap.add_argument('--workdir', default=os.path.join(HERE, 'extracted'))
    ap.add_argument('--only', default=None, help='comma-separated shader filter for transcribe')
    args = ap.parse_args(argv)

    base = os.path.join(args.install, 'base')
    work = os.path.join(args.workdir, args.level)
    out = os.path.join(base, 'subtitles')

    run([sys.executable, os.path.join(HERE, 'extract_level.py'),
         '--base', base, '--level', args.level, '--out', work])

    cmd = [sys.executable, os.path.join(HERE, 'transcribe.py'),
           '--manifest', os.path.join(work, 'manifest.json'),
           '--out', out, '--server', args.server]
    if args.speakers:
        cmd += ['--speakers', args.speakers]
    if args.only:
        cmd += ['--only', args.only]
    run(cmd)
    print('\nsubtitle JSONs written to %s' % out)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
