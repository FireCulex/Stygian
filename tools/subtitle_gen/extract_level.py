#!/usr/bin/env python3
"""
extract_level.py — extract all voice-over WAVs referenced by a level's sound
shaders out of a Doom 3 BFG game install.

Part of the subtitle generator (tools/subtitle_gen/). Pure stdlib; reuses
idwav2wav.idwav_to_wave for generated/*.idwav -> RIFF/WAVE conversion.

Usage:
    python3 extract_level.py --base "<game install>/base" --map mars_city2 \
        --out extracted/mars_city2
    python3 extract_level.py --base "<game install>/base" --level mars_city2 \
        --out extracted/mars_city2        (legacy: --level is just the vo dir)

--map discovers the level's voice-over directories automatically (see
discover_map_vo_dirs); --level keeps only 'sound/vo/<level>/'.

A shader is included when any of its sound files lives under one of the
level's 'sound/vo/<dir>/' directories, or when the level's .map references a
raw wav path directly as "s_shader". Output:

    <out>/<shader>.wav        playable RIFF/WAVE (ADPCM preserved; ffmpeg and
                              whisper.cpp both decode MS ADPCM WAVs)
    <out>/manifest.json       [{shader, sounds: [{resource, wav, source}]}]

Nothing is ever written into the game install by this script.
"""

import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import d3pack
from idwav2wav import idwav_to_wave

S_SHADER_RE = re.compile(r'"s_shader"\s+"([^"]+)"')
VO_PATH_RE = re.compile(r'sound/vo/([^/]+)/', re.I)

# Resurrection of Evil / Lost Mission vo dirs — never scanned (base game only).
EXPANSION_VO_DIRS = {'erebus1', 'erebus2', 'erebus3', 'erebus4', 'erebus5', 'erebus6',
                     'phobos1', 'phobos2', 'phobos3', 'phobos4',
                     'enpro1', 'enpro2', 'exis', 'hell_post', 'underground'}

# sound/vo/ subdirs that are NOT per-map level dialogue: PDA audio logs,
# video-disk audio, generic AI chatter/taunts, MP ctf, end cinematic.
# Excluded from per-map discovery, but the --all-vo sweep DOES include the
# generic chatter/taunts/feedback dirs (minus ALL_VO_EXCLUDE) so no voice
# line is missed.
GENERIC_VO_DIRS = ({'_generic', 'audio_logs', 'combat_chatter', 'ctf', 'end',
                    'feedback', 'taunts', 'video'} | EXPANSION_VO_DIRS)

# what --all-vo still refuses to touch: PDA audio logs, MP ctf, expansions.
# NOTE: 'video' (sound/vo/video/) is INCLUDED — in BFG the videos are
# sound/vo/video/*.RoQ played with these wavs as their audio track via
# sound/video.sndshd (PDA video disks: SND_CHANNEL_PDA_VIDEO; in-game
# camera cinematics: script-started), so transcribing them IS the
# cinematic subtitle path.
ALL_VO_EXCLUDE = ({'audio_logs', 'ctf'} | EXPANSION_VO_DIRS)


def sanitize(name):
    """Same rule as transcribe.py: shader name -> JSON/wav file base name."""
    return name.replace('/', '_').replace('\\', '_')


def _norm(s):
    return re.sub(r'[^a-z0-9]', '', s.lower())


def all_vo_dirs(pack_tables):
    vos = set()
    for _pack, entries in pack_tables:
        for name in entries:
            m = VO_PATH_RE.search(name)  # pack entries are 'generated/sound/vo/...'
            if m:
                vos.add(m.group(1).lower())
    return vos


def discover_map_vo_dirs(pack_tables, map_name):
    """All sound/vo/ subdirs a map draws dialogue from:
      - the vo dir matching the map name exactly or underscore-insensitively
        (mars_city1 -> marscity, alphalabs4 -> alpha_labs4, ...)
      - vo paths used as raw "s_shader" values in maps/game/<map>.map
      - decls in sound/<map>.sndshd / sound/map_<map>.sndshd
    Generic/system/expansion dirs (GENERIC_VO_DIRS) are excluded.
    """
    nmp = _norm(map_name)
    vos = set()
    for v in all_vo_dirs(pack_tables):
        if _norm(v) == nmp:
            vos.add(v)
    for pack, entries in pack_tables:
        for name, (off, ln) in entries.items():
            if not name.endswith('.sndshd') or not name.startswith('sound/'):
                continue
            stem = _norm(name[len('sound/'):-len('.sndshd')])
            if stem.startswith('map'):
                stem = stem[3:]
            if stem == nmp:
                text = d3pack.read_entry(pack, off, ln).decode('latin1')
                for m in VO_PATH_RE.finditer(text):
                    vos.add(m.group(1).lower())
    map_key = 'maps/game/%s.map' % map_name
    for pack, entries in pack_tables:
        if map_key not in entries:
            continue
        off, ln = entries[map_key]
        text = d3pack.read_entry(pack, off, ln).decode('latin1', errors='replace')
        for m in S_SHADER_RE.finditer(text):
            val = m.group(1).replace('\\', '/').lower()
            m2 = VO_PATH_RE.match(val)
            if m2:
                vos.add(m2.group(1).lower())
        break
    return {v for v in vos if v not in GENERIC_VO_DIRS}


def collect_shaders(base_dir, vo_dirs, map_name=None):
    """Scan every .resources pack for .sndshd decls referencing any of the
    vo dirs, plus raw "s_shader" wav paths in the map. Returns
    (pack_tables, {shader: [sound_resource_path, ...]})."""
    prefixes = ['sound/vo/%s/' % v for v in sorted(vo_dirs)]
    pack_tables = []
    shaders = {}
    for pack in d3pack.iter_resource_packs(base_dir):
        try:
            entries = d3pack.read_table(pack)
        except (ValueError, OSError) as e:
            print('skip pack %s: %s' % (pack, e))
            continue
        pack_tables.append((pack, entries))
        for name, (off, ln) in entries.items():
            if not name.endswith('.sndshd'):
                continue
            text = d3pack.read_entry(pack, off, ln).decode('latin1')
            for shader, sounds in d3pack.parse_sndshd(text).items():
                vo_sounds = [s for s in sounds
                             if any(s.lower().startswith(p) for p in prefixes)]
                if vo_sounds:
                    shaders.setdefault(shader, [])
                    for s in vo_sounds:
                        if s not in shaders[shader]:
                            shaders[shader].append(s)
    # Some entities reference a raw wav path directly as "s_shader"
    # (e.g. "s_shader" "sound/VO/mars_city2/greg_discussion.wav") — the engine
    # passes that literal string to StartSoundShader, so the subtitle JSON
    # must be keyed by the same string. Scan the level .map for those.
    if map_name:
        map_key = 'maps/game/%s.map' % map_name
        for pack, entries in pack_tables:
            if map_key not in entries:
                continue
            off, ln = entries[map_key]
            text = d3pack.read_entry(pack, off, ln).decode('latin1', errors='replace')
            for m in S_SHADER_RE.finditer(text):
                val = m.group(1).replace('\\', '/').lower()
                m2 = VO_PATH_RE.match(val)
                if m2 and m2.group(1).lower() not in GENERIC_VO_DIRS:
                    shaders.setdefault(val, [])
                    if val not in shaders[val]:
                        shaders[val].append(val)
            break
    return pack_tables, shaders


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--base', required=True, help='game install base/ dir')
    ap.add_argument('--map', default=None, help='map name, e.g. mars_city2 (auto-discovers its vo dirs)')
    ap.add_argument('--level', default=None, help='legacy: only sound/vo/<level>/')
    ap.add_argument('--out', required=True, help='output dir for wavs + manifest.json')
    args = ap.parse_args(argv)

    if not args.map and not args.level:
        ap.error('one of --map / --level is required')

    os.makedirs(args.out, exist_ok=True)
    # probe packs once for discovery
    probe = []
    for pack in d3pack.iter_resource_packs(args.base):
        try:
            probe.append((pack, d3pack.read_table(pack)))
        except (ValueError, OSError):
            pass
    vo_dirs = set()
    map_name = args.map
    if args.map:
        vo_dirs |= discover_map_vo_dirs(probe, args.map)
        print('map %s -> vo dirs: %s' % (args.map, sorted(vo_dirs) or 'NONE'))
    if args.level:
        vo_dirs.add(args.level)
        if not map_name:
            map_name = args.level
    if not vo_dirs:
        print('no vo dirs found for %s under %s' % (map_name, args.base))
        return 1
    pack_tables, shaders = collect_shaders(args.base, vo_dirs, map_name)
    if not shaders:
        print('no shaders referencing %s found under %s'
              % (sorted(vo_dirs), args.base))
        return 1

    manifest = []
    for shader in sorted(shaders):
        entry = {'shader': shader, 'sounds': []}
        for i, res_path in enumerate(shaders[shader]):
            hit = d3pack.find_sound_resource(pack_tables, res_path)
            base = sanitize(shader)
            if not base.endswith('.wav'):
                base += '.wav'
            wav_name = base if len(shaders[shader]) == 1 \
                else '%s__%d.wav' % (base[:-4], i)
            out_wav = os.path.join(args.out, wav_name)
            if hit:
                pack, key, off, ln = hit
                res = idwav_to_wave(d3pack.read_entry(pack, off, ln))
                if not res:
                    print('PARSE FAIL: %s!%s (%s)' % (pack, key, shader))
                    continue
                wave, tag, ch, rate, bits = res
                with open(out_wav, 'wb') as fh:
                    fh.write(wave)
                entry['sounds'].append({'resource': res_path, 'wav': wav_name,
                                        'source': '%s!%s' % (os.path.basename(pack), key),
                                        'codec': 'adpcm' if tag == 2 else 'pcm',
                                        'rate': rate, 'channels': ch})
                print('wrote %-40s <- %s!%s' % (wav_name, os.path.basename(pack), key))
            else:
                loose = d3pack.find_loose_file(args.base, res_path)
                if loose:
                    with open(loose, 'rb') as fh:
                        data = fh.read()
                    with open(out_wav, 'wb') as fh:
                        fh.write(data)
                    entry['sounds'].append({'resource': res_path, 'wav': wav_name,
                                            'source': 'loose:%s' % loose, 'codec': 'wav'})
                    print('wrote %-40s <- loose %s' % (wav_name, loose))
                else:
                    print('NOT FOUND: %s (%s)' % (res_path, shader))
        if entry['sounds']:
            manifest.append(entry)

    manifest_path = os.path.join(args.out, 'manifest.json')
    with open(manifest_path, 'w') as fh:
        json.dump({'level': args.level, 'base': args.base, 'entries': manifest}, fh, indent=2)
    print('\n%d shaders -> %s' % (len(manifest), manifest_path))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
