"""The War Thunder gun sounds agree everywhere: src/wt_clips.h (CREW_WT_CLIPS) == the source table of tools/wt_sound.py ==
the files under assets/sound/wt (no more, no fewer), each 16-bit PCM 48 kHz mono / stereo (`sub`: mono), the set within the
size limit, SOURCE.md naming every file. Needs no source recordings. Run: python -I tests/wt_sound_test.py"""
from __future__ import annotations

import os
import sys
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import wt_sound  # noqa: E402

OUT = os.path.join(ROOT, 'assets', 'sound', 'wt')
LIMIT = 40 * 1024 * 1024


def main() -> int:
    errors = []
    want = wt_sound.clip_table()
    have = {n: len(v) for n, v in wt_sound.SOURCES.items()}
    if want != have:
        errors.append(f'src/wt_clips.h and tools/wt_sound.py differ: {sorted(set(want) ^ set(have))} '
                      f'{[n for n in want if n in have and want[n] != have[n]]}')
    expected = {f'{n}_{i}.wav' for n, k in want.items() for i in range(1, k + 1)}
    files = {f for f in os.listdir(OUT) if f.endswith('.wav')}
    errors += [f'missing {f}' for f in sorted(expected - files)] + [f'unexpected {f}' for f in sorted(files - expected)]
    with open(os.path.join(OUT, 'SOURCE.md'), encoding='utf-8') as f:
        notes = f.read()
    total = 0
    for name in sorted(expected & files):
        path = os.path.join(OUT, name)
        total += os.path.getsize(path)
        if f'| {name} |' not in notes:
            errors.append(f'SOURCE.md does not list {name}')
        with wave.open(path, 'rb') as w:
            channels, width, rate, frames = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        if width != 2 or rate != 48000 or channels not in (1, 2) or frames <= 0:
            errors.append(f'{name}: {width * 8}-bit {rate} Hz {channels} ch {frames} frames')
        if name.rsplit('_', 1)[0] == 'sub' and channels != 1:
            errors.append(f'{name}: sub must be mono, has {channels} channels')
    if total > LIMIT:
        errors.append(f'total {total} bytes over {LIMIT}')
    if wt_sound.check(OUT):
        errors += wt_sound.check(OUT)
    for e in errors:
        print('FAIL', e)
    print(f'{len(expected)} expected, {len(files)} found, {total} bytes, {len(errors)} errors')
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
