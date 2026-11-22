#!/usr/bin/env python3
"""
extract_level.py — extract all voice-over WAVs referenced by a level's sound
shaders out of a Doom 3 BFG game install.

Part of the subtitle generator (tools/subtitle_gen/). Pure stdlib; reuses
idwav2wav.idwav_to_wave for generated/*.idwav -> RIFF/WAVE conversion.

Usage:
    python3 extract_level.py --base "<game install>/base" --level mars_city2 \
        --out extracted/mars_city2

A shader is included when any of its sound files lives under
'sound/vo/<level>/' (the level's voice-over directory). Output:

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


def sanitize(name):
    """Same rule as transcribe.py: shader name -> JSON/wav file base name."""
    return name.replace('/', '_').replace('\\', '_')


def collect_shaders(base_dir, level):
    """Scan every .resources pack for .sndshd decls referencing the level's
    vo directory. Returns {shader: [sound_resource_path, ...]}."""
    vo_prefix = 'sound/vo/%s/' % level
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
                vo_sounds = [s for s in sounds if s.lower().startswith(vo_prefix)]
                if vo_sounds:
                    shaders.setdefault(shader, [])
                    for s in vo_sounds:
                        if s not in shaders[shader]:
                            shaders[shader].append(s)
    # Some entities reference a raw wav path directly as "s_shader"
    # (e.g. "s_shader" "sound/VO/mars_city2/greg_discussion.wav") — the engine
    # passes that literal string to StartSoundShader, so the subtitle JSON
    # must be keyed by the same string. Scan the level .map for those.
    map_key = 'maps/game/%s.map' % level
    for pack, entries in pack_tables:
        if map_key not in entries:
            continue
        off, ln = entries[map_key]
        text = d3pack.read_entry(pack, off, ln).decode('latin1', errors='replace')
        for m in S_SHADER_RE.finditer(text):
            val = m.group(1).replace('\\', '/').lower()
            if val.startswith('sound/') and '/%s/' % level in val:
                shaders.setdefault(val, [])
                if val not in shaders[val]:
                    shaders[val].append(val)
        break
    return pack_tables, shaders


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--base', required=True, help='game install base/ dir')
    ap.add_argument('--level', required=True, help='level vo dir, e.g. mars_city2')
    ap.add_argument('--out', required=True, help='output dir for wavs + manifest.json')
    args = ap.parse_args(argv)

    os.makedirs(args.out, exist_ok=True)
    pack_tables, shaders = collect_shaders(args.base, args.level)
    if not shaders:
        print('no shaders referencing sound/vo/%s/ found under %s' % (args.level, args.base))
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
