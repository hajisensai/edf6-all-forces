"""War Thunder tank / machine-gun gun sounds for the plugin's WT gunfire (src/wt_clips.h CREW_WT_CLIPS), cut from
Gaijin's official FMOD project for modders (GaijinEntertainment/fmod_studio_warthunder_for_modders, see COMMIT).
The processed WAVs and assets/sound/wt/SOURCE.md are committed (the repository owner allowed it); the installer puts them in
Mods/Plugins/EDF6VehicleCrew_wt/<name>_<i>.wav (i from 1), which is where the plugin reads them.

  python -I tools/wt_sound.py --src <extracted gaijin_fmod_project> [--out assets/sound/wt]
  python -I tools/wt_sound.py --check [--out assets/sound/wt]     tables and committed files agree (needs no source)

The source is untrusted downloaded data: run with -I and pass the path as an argument.
Each file: 16-bit PCM read, channels kept (`sub`: mixed down to mono), resampled to 48 kHz, 20 Hz high-pass. No per-file
levelling or normalising (the clips' relative levels are what the FMOD events' dB values assume); when the whole set's
peak is over 0.98 full scale every file is scaled by the same gain. The start is kept (layers of one event line up by
their starts); the tail under -60 dBFS is cut, with a 20 ms fade-out; a clip with a maximum length is cut to it with a
300 ms fade-out. Output: 16-bit PCM WAV, never lossy-coded again (the source is already vorbis).
"""
from __future__ import annotations

import argparse
import os
import re
import sys
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CLIPS_H = os.path.join(ROOT, 'src', 'wt_clips.h')
DEFAULT_OUT = os.path.join(ROOT, 'assets', 'sound', 'wt')
COMMIT = '07fd12efffbf20b2053d630ed5cf7ca3ddfa54a6'
UPSTREAM = 'https://github.com/GaijinEntertainment/fmod_studio_warthunder_for_modders'
RATE = 48000
PEAK_LIMIT = 0.98
TAIL_FLOOR = 10 ** (-60 / 20)
TAIL_FADE = 0.020
CUT_FADE = 0.300
MAX_MB = 40
BASE = 'tanks/weapon/'
CLOSE = 'cannons/cannons_close_modules/close_17_12_2021/'
TAILS = 'cannons/cannon_common_tails/'
FAR = 'cannons/cannons_far/'
M2 = 'mg/oneshot/m2/grains/'
MG = 'mg/oneshot/mg_series/grains/'

# (name, directory under BASE, file name with {} for the 1-based number, variants)
_SPEC = (
    ('shot_mid', CLOSE, 'cannon_02_shot-{:03d}', 3), ('shot_big', CLOSE, 'cannon_01_shot-{:03d}', 3),
    ('shot_heavy', CLOSE, 'cannon_07_shot-{:03d}', 3), ('middle_mid', CLOSE, 'cannon_02_middle-{:03d}', 2),
    ('middle_big', CLOSE, 'cannon_01_middle-{:03d}', 2), ('bass', CLOSE, 'cannon_05_bass-{:03d}', 2),
    ('body_big', 'cannons/cannons_close_modules/', 'body_big_01-{:03d}', 2),
    ('metal', 'cannons/cannons_close_modules/metal/', 'metal_body_big_02-{:03d}', 2),
    ('mech_howitzer', 'cannons/', 'howitzer_shot_mech_{:02d}', 2),
    ('close_75', 'cannons/cannon_75mm_kwk40/', 'cannon_75mm_kwk40_shot_{:02d}', 3),
    ('close_122', 'cannons/cannon_122mm_d25t/', 'cannon_122mm_d25t_shot_{:02d}', 3),
    ('close_128', 'cannons/cannon_128mm_pak44/', 'cannon_128mm_pak44_shot_{:02d}', 3),
    ('close_150', 'cannons/cannon_150mm_sfh_18/', '150mm_sfh_18_close-{:03d}', 3),
    ('far_75', 'cannons/cannon_75mm_kwk40/', 'cannon_75mm_kwk40_shot_far_{:02d}', 3),
    ('far_122', 'cannons/cannon_122mm_d25t/', 'cannon_122mm_d25t_shot_far_{:02d}', 3),
    ('far_128', 'cannons/cannon_128mm_pak44/', 'cannon_128mm_pak44_shot_far_{:02d}', 3),
    ('far_150', 'cannons/cannon_150mm_sfh_18/', '150mm_sfh_18_dist-{:03d}', 3),
    ('bump_med', FAR, 'cannon_ground_far_med_add-{:03d}', 3), ('bump_big', FAR, 'cannon_ground_far_big_add-{:03d}', 3),
    ('sub', TAILS, 'cannons_sub-{:03d}', 3),
    ('tail_mid', TAILS, 'cannon_84_94_tail_far_{:02d}', 2), ('tail_big', TAILS, 'cannon_100_122_tail_far_{:02d}', 2),
    ('tail_heavy', TAILS, 'cannon_128_150_tail_far_{:02d}', 2),
    ('m2_close', M2, 'm2_close-{:03d}', 6), ('m2_far', M2, 'm2_far-{:03d}', 6),
    ('m2_last_close', M2, 'm2_last_close-{:03d}', 2), ('m2_last_far', M2, 'm2_last_far-{:03d}', 2),
    ('mg42_close', MG, 'mg42_close-{:03d}', 6), ('mg42_far', MG, 'mg42_far-{:03d}', 6),
    ('mg42_last_close', MG, 'mg42_last_close-{:03d}', 2), ('mg42_last_far', MG, 'mg42_last_far-{:03d}', 2),
)
SOURCES = {n: [BASE + d + f.format(i) + '.wav' for i in range(1, k + 1)] for n, d, f, k in _SPEC}
MONO = {'sub'}
MAX_SECONDS = {'close_150': 5.0, 'tail_heavy': 7.0}


def clip_table(path: str = CLIPS_H) -> dict[str, int]:
    with open(path, encoding='utf-8') as f:
        text = f.read()
    found = re.findall(r'X\((\w+),(\d+)\)', text)
    table = {n: int(k) for n, k in found}
    if len(table) != len(found):
        raise SystemExit(f'{path}: a clip name is listed twice')
    return table


def validate_tables() -> dict[str, int]:
    want = clip_table()
    have = {n: len(v) for n, v in SOURCES.items()}
    if want != have:
        diff = sorted(set(want) ^ set(have)) + sorted(n for n in want if n in have and want[n] != have[n])
        raise SystemExit(f'src/wt_clips.h and the source table of tools/wt_sound.py differ: {diff}')
    return want


def out_names() -> list[str]:
    return [f'{n}_{i}.wav' for n, v in SOURCES.items() for i in range(1, len(v) + 1)]


def read_wav(path: str):
    import numpy as np
    with wave.open(path, 'rb') as w:
        ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if width != 2:
        raise SystemExit(f'{path}: {width * 8}-bit, expected 16-bit PCM')
    x = np.frombuffer(raw, '<i2').astype(np.float64) / 32768
    return x.reshape(-1, ch), rate


def process(path: str, mono: bool, max_seconds: float):
    """Float samples (frames, channels) at 48 kHz, 20 Hz high-passed, cut to max_seconds with a fade; no gain yet."""
    import numpy as np
    sys.path.insert(0, HERE)
    import sound_pack
    x, rate = read_wav(path)
    if mono and x.shape[1] > 1:
        x = x.mean(axis=1, keepdims=True)
    cols = []
    for c in range(x.shape[1]):
        y = x[:, c]
        if rate != RATE:
            y = sound_pack.resample(y, RATE / rate)
        cols.append(sound_pack.highpass(y, 20.0, RATE))
    y = np.stack(cols, axis=1)
    if max_seconds and len(y) > int(max_seconds * RATE):
        y = y[:int(max_seconds * RATE)].copy()
        fade = int(CUT_FADE * RATE)
        y[-fade:] *= np.linspace(1.0, 0.0, fade)[:, None]
    return y


def finish(y, gain: float):
    import numpy as np
    y = y * gain
    loud = np.nonzero(np.abs(y).max(axis=1) > TAIL_FLOOR)[0]
    end = int(loud[-1]) + 1 if len(loud) else 1
    keep = min(len(y), end + int(TAIL_FADE * RATE))
    y = y[:keep].copy()
    fade = min(len(y), int(TAIL_FADE * RATE))
    y[-fade:] *= np.linspace(1.0, 0.0, fade)[:, None]
    return y


def write_wav(path: str, y) -> None:
    import numpy as np
    pcm = np.clip(np.round(y * 32767), -32768, 32767).astype('<i2')
    with wave.open(path, 'wb') as w:
        w.setnchannels(pcm.shape[1])
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())


def build(src: str, out: str) -> None:
    import numpy as np
    validate_tables()
    done = {}
    for name, files in SOURCES.items():
        for i, rel in enumerate(files, 1):
            path = os.path.join(src, *rel.split('/'))
            if not os.path.isfile(path):
                raise SystemExit(f'missing source: {rel}')
            done[f'{name}_{i}.wav'] = (rel, process(path, name in MONO, MAX_SECONDS.get(name, 0.0)))
    peak = max(float(np.abs(y).max()) for _, y in done.values())
    gain = min(1.0, PEAK_LIMIT / peak)
    os.makedirs(out, exist_ok=True)
    for stale in os.listdir(out):
        if stale.endswith('.wav') and stale not in done:
            os.remove(os.path.join(out, stale))
    lines, total = [], 0
    for fn, (rel, y) in done.items():
        z = finish(y, gain)
        write_wav(os.path.join(out, fn), z)
        size = os.path.getsize(os.path.join(out, fn))
        total += size
        lines.append(f'| {fn} | {rel} | {z.shape[1]} | {len(z) / RATE:.3f} | {size} |')
    head = (
        '# War Thunder gun sounds (assets/sound/wt)\n\n'
        f'Source: {UPSTREAM} , commit `{COMMIT}`.\n'
        'The upstream Readme says the project "allows you to create sound modifications" and that using the content means\n'
        'agreeing to the War Thunder contribution agreement (https://live.warthunder.com/contribution_agreement/?lang=en).\n'
        'The repository owner allowed committing the processed clips; the terms are those of the upstream repository.\n'
        'The upstream assets are already vorbis-compressed (ogg renamed to .wav); they are decoded once here and written as\n'
        '16-bit PCM.\n\n'
        'Made by `tools/wt_sound.py` (clip names and variant counts: src/wt_clips.h CREW_WT_CLIPS). Per file: channels kept\n'
        f'(`sub`: mixed to mono), resampled to {RATE} Hz, 20 Hz high-pass, no per-file levelling or normalising, start kept,\n'
        f'tail under -60 dBFS cut with a {int(TAIL_FADE * 1000)} ms fade-out; close_150 is cut to 5.0 s and tail_heavy to 7.0 s\n'
        '(300 ms fade-out).\n'
        f'One gain for the whole set: **{gain:.6f}** (set peak before gain {peak:.6f}; scaled only when over {PEAK_LIMIT}).\n\n'
        'Installed to `Mods/Plugins/EDF6VehicleCrew_wt/<name>_<i>.wav` by the installer.\n\n'
        f'Files: {len(done)}, total size {total} bytes ({total / 1048576:.2f} MiB).\n\n'
        '| file | source (under the project root) | channels | seconds | bytes |\n|---|---|---|---|---|\n')
    with open(os.path.join(out, 'SOURCE.md'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(head + '\n'.join(lines) + '\n')
    print(f'{len(done)} files, {total} bytes ({total / 1048576:.2f} MiB), gain {gain:.6f}')
    if total > MAX_MB * 1048576:
        raise SystemExit(f'over the {MAX_MB} MB limit')


def check(out: str) -> list[str]:
    errors = []
    validate_tables()
    names = set(out_names())
    have = {f for f in os.listdir(out) if f.endswith('.wav')}
    for f in sorted(names - have):
        errors.append(f'missing {f}')
    for f in sorted(have - names):
        errors.append(f'unexpected {f}')
    md = os.path.join(out, 'SOURCE.md')
    text = open(md, encoding='utf-8').read() if os.path.isfile(md) else ''
    total = 0
    for f in sorted(names & have):
        p = os.path.join(out, f)
        total += os.path.getsize(p)
        if f'| {f} |' not in text:
            errors.append(f'SOURCE.md does not list {f}')
        with wave.open(p, 'rb') as w:
            ch, width, rate = w.getnchannels(), w.getsampwidth(), w.getframerate()
        if width != 2 or rate != RATE or ch not in (1, 2) or (f.rsplit('_', 1)[0] in MONO and ch != 1):
            errors.append(f'{f}: {ch} ch, {width * 8}-bit, {rate} Hz')
    if total > MAX_MB * 1048576:
        errors.append(f'total {total} bytes over {MAX_MB} MB')
    return errors


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--src')
    ap.add_argument('--out', default=DEFAULT_OUT)
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args(argv)
    if a.check:
        errors = check(a.out)
        for e in errors:
            print(e)
        return 1 if errors else 0
    if not a.src:
        ap.error('--src is required unless --check')
    build(a.src, a.out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
