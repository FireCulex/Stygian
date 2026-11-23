#!/usr/bin/env python3
"""
d3pack.py — light readers for Doom 3 BFG asset containers.

Part of the subtitle generator tool (tools/subtitle_gen/). Pure stdlib.

Unlike idwav2wav.py (which slurps whole packs), these readers only pull the
12-byte header + directory table and then individual entries, so scanning
every .resources under a game install stays fast.

.resources directory format (neo/framework/File_Resource.h):
    BE magic 0xD000000D, BE tableOffset, BE tableLength,
    at tableOffset: BE int32 numFiles, then per entry:
      LE int32 nameLen, name bytes, BE int32 offset, BE int32 length

.sndshd format: blocks of
    shaderName {
        ...key value...        (ignored here)
        sound/path/to.wav      (one or more sound files)
    }
    // line comments
"""

import os
import struct

RESOURCE_MAGIC = 0xD000000D


def iter_resource_packs(base_dir):
    """Yield all .resources paths under base_dir (recursive), sorted."""
    packs = []
    for root, _dirs, files in os.walk(base_dir):
        for f in files:
            if f.endswith('.resources'):
                packs.append(os.path.join(root, f))
    return sorted(packs)


def read_table(path):
    """Parse only the directory table of a .resources pack.

    Returns {normalized_name: (offset, length)}. Names are lowercased and
    backslashes normalized to '/', matching engine lookup behavior.
    """
    with open(path, 'rb') as fh:
        hdr = fh.read(12)
        if len(hdr) < 12:
            raise ValueError('truncated header: %s' % path)
        magic, toff, _tlen = struct.unpack('>III', hdr)
        if magic != RESOURCE_MAGIC:
            raise ValueError('not a .resources file: %s' % path)
        fh.seek(toff)
        num, = struct.unpack('>i', fh.read(4))
        entries = {}
        for _ in range(num):
            ln, = struct.unpack('<i', fh.read(4))
            name = fh.read(ln).decode('latin1')
            off, length = struct.unpack('>ii', fh.read(8))
            entries[name.replace('\\', '/').lower()] = (off, length)
    return entries


def read_entry(path, off, length):
    with open(path, 'rb') as fh:
        fh.seek(off)
        return fh.read(length)


def parse_sndshd(text):
    """Parse .sndshd text -> {shader_name: [sound_resource_path, ...]}."""
    shaders = {}
    cur = None
    for raw in text.splitlines():
        i = raw.find('//')
        if i >= 0:
            raw = raw[:i]
        s = raw.strip()
        if not s:
            continue
        if s.endswith('{'):
            cur = s[:-1].strip()
            if not cur:
                cur = None
                continue
            shaders.setdefault(cur, [])
        elif s == '}':
            cur = None
        elif cur is not None and s.startswith('sound/'):
            shaders[cur].append(s)
    return shaders


def find_sound_resource(pack_tables, resource_path):
    """Locate a sound resource (e.g. 'sound/vo/x/y.wav') across packs.

    pack_tables: list of (pack_path, entries_dict). Looks for the generated
    .idwav first ('generated/<path-without-ext>.idwav'), then a verbatim
    copy of the path inside a pack.

    Returns (pack_path, key, off, length) or None.
    """
    p = resource_path.replace('\\', '/').lower()
    stem = p[:-4] if p.endswith('.wav') else p
    candidates = ['generated/%s.idwav' % stem, p, stem + '.idwav']
    for pack_path, entries in pack_tables:
        for cand in candidates:
            if cand in entries:
                off, ln = entries[cand]
                return pack_path, cand, off, ln
    return None


def find_loose_file(base_dir, resource_path):
    """Loose-file fallback: base_dir/<resource_path>."""
    p = os.path.join(base_dir, resource_path.replace('/', os.sep))
    return p if os.path.isfile(p) else None
