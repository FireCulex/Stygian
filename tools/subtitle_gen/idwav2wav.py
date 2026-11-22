#!/usr/bin/env python3
"""
idwav2wav.py — extract playable WAV files from Doom 3 BFG .resources packs.

Part of the subtitle generator tool (tools/subtitle_gen/). Pure Python, no
engine build required. Two container formats are handled:

1. .resources packs (neo/framework/File_Resource.h):
     BE magic 0xD000000D, BE tableOffset, BE tableLength,
     at tableOffset: BE numFiles, then per entry:
       LE int32 nameLen, name bytes, BE int32 offset, BE int32 length

2. generated/*.idwav entries inside the pack (writer:
   idSoundSample_XAudio2::WriteGeneratedSample; loader reference:
   neo/sound/sdl/sdl_audio.cpp, idSoundSample_SDL::LoadGenerated):
     BE magic 'mzrt', BE timestamp, BE loaded, BE playBegin, BE playLength,
     pad byte(s), LE waveFmt basic (16 bytes) [MS ADPCM: LE extraSize + extra],
     BE ampnum + amplitude bytes, BE totalBufferSize, BE numBuffers,
     per buffer: BE numSamples, BE bufferSize, raw payload.
   The payload is re-wrapped as a synthetic RIFF/WAVE (PCM or MS ADPCM),
   which any player / ffmpeg / Whisper frontend can decode.

CLI:
    python3 idwav2wav.py <game_base_dir> <out_dir> <name> [<name> ...]
where <name> is a resource-relative path without the "generated/" prefix and
without the ".idwav" extension, e.g.  sound/vo/mars_city2/com_video_request
"""

import os
import struct
import sys

RESOURCE_MAGIC = 0xD000000D
IDWAV_MAGIC = 0x6D7A7274  # 'mzrt', big-endian


def be32(b, o):
    return struct.unpack('>I', b[o:o + 4])[0]


def le16(b, o):
    return struct.unpack('<H', b[o:o + 2])[0]


def le32(b, o):
    return struct.unpack('<I', b[o:o + 4])[0]


def parse_resources(path):
    """Parse a .resources pack -> {name: (offset, length)} plus raw data."""
    data = open(path, 'rb').read()
    magic, toff, _tlen = struct.unpack('>iii', data[:12])
    if (magic & 0xFFFFFFFF) != RESOURCE_MAGIC:
        raise ValueError('not a .resources file: %s' % path)
    num, = struct.unpack('>i', data[toff:toff + 4])
    pos = toff + 4
    entries = {}
    for _ in range(num):
        ln, = struct.unpack('<i', data[pos:pos + 4])
        name = data[pos + 4:pos + 4 + ln].decode('latin1')
        off, fln = struct.unpack('>ii', data[pos + 4 + ln:pos + 12 + ln])
        entries[name] = (off, fln)
        pos = pos + 12 + ln
    return data, entries


def idwav_to_wave(b):
    """Re-wrap a generated .idwav payload as RIFF/WAVE bytes.

    Returns (wave_bytes, tag, channels, rate, bits) or None on parse failure.
    """
    ln = len(b)
    if ln < 64 or be32(b, 0) != IDWAV_MAGIC:
        return None
    fmt_off = -1
    tag = channels = rate = block = bits = 0
    for o in range(16, 33):
        if o + 16 > ln:
            break
        t = le16(b, o)
        ch = le16(b, o + 2)
        r = le32(b, o + 4)
        blk = le16(b, o + 12)
        bt = le16(b, o + 14)
        if (t in (1, 2)) and ch in (1, 2) and 4000 <= r <= 96000 \
                and 0 < blk < 8192 \
                and ((t == 1 and bt in (8, 16)) or (t == 2 and bt == 4)):
            fmt_off, tag, channels, rate, block, bits = o, t, ch, r, blk, bt
            break
    if fmt_off < 0:
        return None
    p = fmt_off + 16
    extra = b''
    if tag == 2:  # MS ADPCM: LE extraSize + verbatim extra bytes
        extra_size = le16(b, p)
        p += 2
        extra = b[p:p + extra_size]
        p += extra_size
    ampnum = be32(b, p)
    p += 4 + ampnum          # amplitude table (unused for playback)
    p += 4                   # totalBufferSize
    num_buffers = be32(b, p)
    p += 4
    if num_buffers == 0 or num_buffers > 64:
        return None
    payload = bytearray()
    for _ in range(num_buffers):
        p += 4               # numSamples
        bsz = be32(b, p)
        p += 4
        if bsz == 0 or p + bsz > ln:
            return None
        payload += b[p:p + bsz]
        p += bsz
    if not payload:
        return None
    fmt = bytearray(b[fmt_off:fmt_off + 16])  # basic waveFmt is already LE
    if tag == 2:
        fmt += struct.pack('<H', len(extra)) + extra
    wave = bytearray()
    wave += b'RIFF' + struct.pack('<I', 4 + 8 + len(fmt) + 8 + len(payload)) + b'WAVE'
    wave += b'fmt ' + struct.pack('<I', len(fmt)) + fmt
    wave += b'data' + struct.pack('<I', len(payload)) + payload
    return bytes(wave), tag, channels, rate, bits


def find_entry(entries, name):
    """Locate a generated idwav for a resource-relative name (with or without
    the generated/ prefix / .idwav extension). Returns (key, offset, length)."""
    for cand in ('generated/%s.idwav' % name, name, name + '.idwav'):
        if cand in entries:
            off, ln = entries[cand]
            return cand, off, ln
    matches = [k for k in entries if k.endswith('/' + name + '.idwav') or k == name + '.idwav']
    if matches:
        k = sorted(matches)[0]
        off, ln = entries[k]
        return k, off, ln
    return None


def extract(base_dir, out_dir, names):
    """Extract each name from every *.resources pack in base_dir into out_dir."""
    os.makedirs(out_dir, exist_ok=True)
    packs = sorted(f for f in os.listdir(base_dir) if f.endswith('.resources'))
    # map resource packs (maps/**/*.resources) are searched too
    for root, _dirs, files in os.walk(base_dir):
        for f in files:
            if f.endswith('.resources'):
                packs.append(os.path.relpath(os.path.join(root, f), base_dir))
    packs = sorted(set(packs))
    results = []
    for name in names:
        found = False
        for pack in packs:
            try:
                data, entries = parse_resources(os.path.join(base_dir, pack))
            except (ValueError, OSError):
                continue
            hit = find_entry(entries, name)
            if not hit:
                continue
            key, off, ln = hit
            res = idwav_to_wave(data[off:off + ln])
            if not res:
                print('PARSE FAIL: %s!%s' % (pack, key))
                continue
            wave, tag, ch, rate, bits = res
            out = os.path.join(out_dir, os.path.basename(name) + '.wav')
            with open(out, 'wb') as fh:
                fh.write(wave)
            print('wrote %s  (from %s!%s, tag=%d ch=%d rate=%d bits=%d, %d bytes)'
                  % (out, pack, key, tag, ch, rate, bits, len(wave)))
            results.append(out)
            found = True
            break
        if not found:
            print('NOT FOUND in any .resources under %s: %s' % (base_dir, name))
    return results


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 1
    base_dir, out_dir, names = argv[1], argv[2], argv[3:]
    extract(base_dir, out_dir, names)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
