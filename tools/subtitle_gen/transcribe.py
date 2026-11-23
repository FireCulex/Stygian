#!/usr/bin/env python3
"""
transcribe.py — send extracted level WAVs to a whisper.cpp server and emit
line-level subtitle cues in the engine schema (version 2):

    { "speaker": "Sgt. Kelly",                       (optional)
      "cues": [ {"start": 120, "end": 2540, "text": "All remaining marine units, ..."}, ... ] }

Word-level timestamps from whisper.cpp (verbose_json + token_timestamps) are
grouped into cues at forceBreak boundaries: forceBreak is set on words ending
a sentence (. ! ? ...) and on the last word of each whisper segment (segments
usually break at clause/sentence boundaries). Cue text is the words
smart-joined (punctuation tokens re-attached, e.g. "Kelly" + "." -> "Kelly.")
or — when a cue covers exactly one whole whisper segment — the segment's own
text, which avoids mid-word splits like "reg" + "roup".
Times are integer milliseconds relative to sound start.

Output: by default ONLY the combined <out>/subtitles.json is written (the
file the engine loads via ui_subtitleFile). Entries for shaders re-processed
in this run are merged in (updated, or removed when the shader now skips),
so --only runs don't clobber the rest. --per-shader additionally writes one
<out>/<sanitized_shader>.json per shader (sanitized = '/' and '\\' -> '_')
as a review artifact; the engine never reads those.

Raw server responses are kept next to the manifest as
<shader>.whisper.json for manual review.

Usage:
    python3 transcribe.py --manifest extracted/mars_city2/manifest.json \
        --out "<game install>/base/subtitles" \
        --server http://127.0.0.1:8081 --speakers speakers_marscity2.json

Pure stdlib (urllib multipart POST). Nothing is written into the repo.
"""

import argparse
import json
import os
import re
import sys
import urllib.request
import uuid

SENTENCE_END = ('.', '!', '?', '…')
# Whisper's "no speech / non-verbal" hallucinations look like "[ ... ]".
BRACKETED_RE = re.compile(r'^\[.*\]$')
# Smart-join rules: whisper emits punctuation as its own word tokens, so
# re-attach them to the neighbouring word instead of leaving a space.
NO_SPACE_BEFORE = re.compile(r'\s+([.,!?;:%\)\]\}"\'])')
NO_SPACE_AFTER = re.compile(r'([\(\[\{])\s+')
WS_RE = re.compile(r'\s+')


def sanitize(name):
    return name.replace('/', '_').replace('\\', '_')


def post_inference(server, wav_path, language, temperature):
    boundary = '----subtitlegen' + uuid.uuid4().hex
    with open(wav_path, 'rb') as fh:
        audio = fh.read()
    body = bytearray()
    fields = {
        'response_format': 'verbose_json',
        'token_timestamps': 'true',
        'language': language,
        'temperature': str(temperature),
    }
    for k, v in fields.items():
        body += ('--%s\r\nContent-Disposition: form-data; name="%s"\r\n\r\n%s\r\n'
                 % (boundary, k, v)).encode('utf-8')
    body += ('--%s\r\nContent-Disposition: form-data; name="file"; filename="%s"\r\n'
             'Content-Type: audio/wav\r\n\r\n'
             % (boundary, os.path.basename(wav_path))).encode('utf-8')
    body += audio
    body += ('\r\n--%s--\r\n' % boundary).encode('utf-8')
    req = urllib.request.Request(
        server.rstrip('/') + '/inference', data=bytes(body),
        headers={'Content-Type': 'multipart/form-data; boundary=%s' % boundary})
    with urllib.request.urlopen(req, timeout=900) as resp:
        return json.loads(resp.read().decode('utf-8'))


def build_words(verbose):
    """verbose_json -> [{phrase, startTime, endTime, forceBreak, seg, ...}] (ms)."""
    words = []
    segments = verbose.get('segments', [])
    for si, seg in enumerate(segments):
        seg_words = seg.get('words', [])
        for wi, w in enumerate(seg_words):
            phrase = (w.get('word') or '').strip()
            if not phrase:
                continue
            start_ms = int(round(float(w.get('start', 0.0)) * 1000.0))
            end_ms = int(round(float(w.get('end', w.get('start', 0.0))) * 1000.0))
            tail = phrase.rstrip('"').rstrip("'")
            force_break = (wi == len(seg_words) - 1) or (bool(tail) and tail[-1] in SENTENCE_END)
            words.append({'phrase': phrase, 'startTime': start_ms, 'endTime': end_ms,
                          'forceBreak': bool(force_break),
                          'seg': si, 'firstInSeg': wi == 0, 'lastInSeg': wi == len(seg_words) - 1})
    return words


def clean_text(phrases):
    """Join word tokens with smart spacing (punctuation re-attached)."""
    text = WS_RE.sub(' ', ' '.join(phrases))
    text = NO_SPACE_BEFORE.sub(r'\1', text)
    text = NO_SPACE_AFTER.sub(r'\1', text)
    return text.strip()


def words_to_cues(verbose, words):
    """Group words into line-level cues at forceBreak boundaries.

    Returns [{start, end, text}] with integer ms times. When a cue covers
    exactly one whole whisper segment, the segment's own text is used
    (avoids mid-word token splits like "reg" + "roup").
    """
    segments = verbose.get('segments', [])
    cues = []
    group = []

    def flush():
        if not group:
            return
        text = ''
        if (group[0]['firstInSeg'] and group[-1]['lastInSeg']
                and len({w['seg'] for w in group}) == 1):
            text = (segments[group[0]['seg']].get('text') or '').strip()
        if not text:
            text = clean_text([w['phrase'] for w in group])
        cues.append({'start': group[0]['startTime'], 'end': group[-1]['endTime'],
                     'text': text})
        del group[:]

    for w in words:
        group.append(w)
        if w['forceBreak']:
            flush()
    flush()

    # repair missing/implausible end times: next cue's start, else +400 ms
    for i, cue in enumerate(cues):
        if cue['end'] <= cue['start']:
            nxt = cues[i + 1]['start'] if i + 1 < len(cues) else cue['start'] + 400
            cue['end'] = max(cue['start'] + 1, nxt)
    return cues


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--manifest', required=True)
    ap.add_argument('--out', required=True, help='subtitle JSON output dir (install base/subtitles)')
    ap.add_argument('--server', default='http://127.0.0.1:8081')
    ap.add_argument('--speakers', default=None, help='JSON map shader -> speaker name')
    ap.add_argument('--language', default='en')
    ap.add_argument('--temperature', type=float, default=0.0)
    ap.add_argument('--only', default=None, help='comma-separated shader filter')
    ap.add_argument('--cache-dir', default=None,
                    help='shared .whisper.json cache dir (default: manifest dir); lets '
                         'per-map runs share transcriptions of shaders used by several maps')
    ap.add_argument('--no-cache', action='store_true',
                    help='re-transcribe even if a cached .whisper.json exists')
    ap.add_argument('--per-shader', action='store_true',
                    help='also write one <shader>.json per shader (review artifacts; '
                         'the engine only reads the combined subtitles.json)')
    args = ap.parse_args(argv)

    with open(args.manifest) as fh:
        manifest = json.load(fh)
    base_dir = os.path.dirname(os.path.abspath(args.manifest))
    speakers = {}
    if args.speakers:
        with open(args.speakers) as fh:
            speakers = json.load(fh)
    only = set(args.only.split(',')) if args.only else None
    os.makedirs(args.out, exist_ok=True)
    cache_dir = args.cache_dir or base_dir
    os.makedirs(cache_dir, exist_ok=True)

    ok = fail = skipped = 0
    combined_updates = {}
    combined_removals = []
    for entry in manifest['entries']:
        shader = entry['shader']
        if only and shader not in only:
            continue
        if not entry['sounds']:
            continue
        wav = os.path.join(base_dir, entry['sounds'][0]['wav'])
        if not os.path.isfile(wav):
            print('SKIP (no wav): %s' % shader)
            continue
        cache_path = os.path.join(cache_dir, sanitize(shader) + '.whisper.json')
        if not args.no_cache and os.path.isfile(cache_path):
            with open(cache_path) as fh:
                verbose = json.load(fh)
        else:
            try:
                verbose = post_inference(args.server, wav, args.language, args.temperature)
            except Exception as e:
                print('FAIL %s: %s' % (shader, e))
                fail += 1
                continue
            with open(cache_path, 'w') as fh:
                json.dump(verbose, fh, indent=2)
        words = build_words(verbose)
        cues = words_to_cues(verbose, words)
        joined = ' '.join(w['phrase'] for w in words).strip()
        if not cues or BRACKETED_RE.match(joined):
            print('SKIP (no speech detected): %s' % shader)
            skipped += 1
            stale = os.path.join(args.out, sanitize(shader) + '.json')
            if os.path.isfile(stale):
                os.remove(stale)
            combined_removals.append(shader)
            continue
        sub = {'cues': cues}
        speaker = speakers.get(shader) or speakers.get(sanitize(shader))
        if speaker:
            sub['speaker'] = speaker
        if args.per_shader:
            out_path = os.path.join(args.out, sanitize(shader) + '.json')
            with open(out_path, 'w') as fh:
                json.dump(sub, fh, indent=2)
        combined_updates[shader] = sub
        text = ' | '.join(c['text'] for c in cues)
        print('OK   %-45s %d cue(s)\n     "%s"' % (shader, len(cues), text[:100]))
        ok += 1

    # Combined file consumed by the engine (ui_subtitleFile default):
    # base/subtitles/subtitles.json = { "version": 2, "sounds": { "<shader>": {...} } }
    # Merge into any existing file so --only runs don't clobber other levels'
    # entries; shaders re-processed as "no speech" are dropped.
    combined_path = os.path.join(args.out, 'subtitles.json')
    sounds = {}
    if os.path.isfile(combined_path):
        try:
            with open(combined_path) as fh:
                old = json.load(fh)
            if isinstance(old, dict) and isinstance(old.get('sounds'), dict):
                sounds = old['sounds']
        except Exception as e:
            print('WARN: could not parse existing %s (%s) - starting fresh' % (combined_path, e))
    for shader in combined_removals:
        sounds.pop(shader, None)
    sounds.update(combined_updates)
    if sounds:
        with open(combined_path, 'w') as fh:
            json.dump({'version': 2, 'generator': 'tools/subtitle_gen',
                       'language': args.language, 'sounds': sounds}, fh, indent=2)
        print('\ncombined %d entries -> %s' % (len(sounds), combined_path))
    print('%d ok, %d failed, %d skipped; subtitles written to %s' % (ok, fail, skipped, args.out))
    return 0 if fail == 0 else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
