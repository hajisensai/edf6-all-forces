"""War Thunder gun, explosion and passby sounds for the plugin's WT sounds (src/wt_clips.h CREW_WT_CLIPS), cut from
Gaijin's official FMOD project for modders (GaijinEntertainment/fmod_studio_warthunder_for_modders, see COMMIT).
The processed WAVs and assets/sound/wt/SOURCE.md are committed (the repository owner allowed it); the installer puts them in
Mods/Plugins/EDF6VehicleCrew_wt/<name>_<i>.wav (i from 1), which is where the plugin reads them.

  python -I tools/wt_sound.py --src <extracted gaijin_fmod_project> [--out assets/sound/wt]
  python -I tools/wt_sound.py --check [--out assets/sound/wt]     tables and committed files agree (needs no source)

The source is untrusted downloaded data: run with -I and pass the path as an argument.
Each file: 16-bit PCM read, channels kept (the MONO set: mixed down to mono), resampled to 48 kHz, 20 Hz high-pass. No per-file
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
MAX_MB = 80
W = 'tanks/weapon/'
CLOSE = W + 'cannons/cannons_close_modules/close_17_12_2021/'
TAILS = W + 'cannons/cannon_common_tails/'
FAR = W + 'cannons/cannons_far/'
M2 = W + 'mg/oneshot/m2/grains/'
MG = W + 'mg/oneshot/mg_series/grains/'
EXM = 'explosion/modules/'
MERGE = EXM + 'layers_merge/'
DEBRIS = 'tanks/impacts_objects/object_material_debris/'
PASSBY = 'tanks/effects/projectile_passby/'
TURRET = 'tanks/effects/turret_move/'
RELOAD = W + 'cannons/cannon_reloading/'
HITS = 'tanks/impacts/hits_recorded/'

# (name, directory under the project root, file name with {} for the 1-based number, variants: a count (1..n) or the
# numbers taken)
_SPEC = (
    # the ground guns (sound_event.h kMedium .. kHowitzer, kM2, kMg42)
    ('shot_mid', CLOSE, 'cannon_02_shot-{:03d}', 3), ('shot_big', CLOSE, 'cannon_01_shot-{:03d}', 3),
    ('shot_heavy', CLOSE, 'cannon_07_shot-{:03d}', 3), ('middle_mid', CLOSE, 'cannon_02_middle-{:03d}', 2),
    ('middle_big', CLOSE, 'cannon_01_middle-{:03d}', 2), ('bass', CLOSE, 'cannon_05_bass-{:03d}', 2),
    ('body_big', W + 'cannons/cannons_close_modules/', 'body_big_01-{:03d}', 2),
    ('metal', W + 'cannons/cannons_close_modules/metal/', 'metal_body_big_02-{:03d}', 2),
    ('mech_howitzer', W + 'cannons/', 'howitzer_shot_mech_{:02d}', 2),
    ('close_75', W + 'cannons/cannon_75mm_kwk40/', 'cannon_75mm_kwk40_shot_{:02d}', 3),
    ('close_122', W + 'cannons/cannon_122mm_d25t/', 'cannon_122mm_d25t_shot_{:02d}', 3),
    ('close_128', W + 'cannons/cannon_128mm_pak44/', 'cannon_128mm_pak44_shot_{:02d}', 3),
    ('close_150', W + 'cannons/cannon_150mm_sfh_18/', '150mm_sfh_18_close-{:03d}', 3),
    ('far_75', W + 'cannons/cannon_75mm_kwk40/', 'cannon_75mm_kwk40_shot_far_{:02d}', 3),
    ('far_122', W + 'cannons/cannon_122mm_d25t/', 'cannon_122mm_d25t_shot_far_{:02d}', 3),
    ('far_128', W + 'cannons/cannon_128mm_pak44/', 'cannon_128mm_pak44_shot_far_{:02d}', 3),
    ('far_150', W + 'cannons/cannon_150mm_sfh_18/', '150mm_sfh_18_dist-{:03d}', 3),
    ('bump_med', FAR, 'cannon_ground_far_med_add-{:03d}', 3), ('bump_big', FAR, 'cannon_ground_far_big_add-{:03d}', 3),
    ('sub', TAILS, 'cannons_sub-{:03d}', 3),
    ('tail_mid', TAILS, 'cannon_84_94_tail_far_{:02d}', 2), ('tail_big', TAILS, 'cannon_100_122_tail_far_{:02d}', 2),
    ('tail_heavy', TAILS, 'cannon_128_150_tail_far_{:02d}', 2),
    ('m2_close', M2, 'm2_close-{:03d}', 6), ('m2_far', M2, 'm2_far-{:03d}', 6),
    ('m2_last_close', M2, 'm2_last_close-{:03d}', 2), ('m2_last_far', M2, 'm2_last_far-{:03d}', 2),
    ('mg42_close', MG, 'mg42_close-{:03d}', 6), ('mg42_far', MG, 'mg42_far-{:03d}', 6),
    ('mg42_last_close', MG, 'mg42_last_close-{:03d}', 2), ('mg42_last_far', MG, 'mg42_last_far-{:03d}', 2),
    # the explosions (sound_event.h kBlastSmall .. kBlastHeavy): explosion_ground_20_37, explosion_ground,
    # art_explosion_ground, bomb_explosion_ground; the tails and the rumble are ours
    ('xs_close', EXM + 'small_hits/', 'hit_explosion_small_close_rapid-{:03d}', 3),
    ('xs_far', 'explosion/', 'bullethit_far_{}', 3),
    ('ex_sweet', 'explosive/', 'aa_explosion_{:02d}_s', 3),
    ('ex_attack', EXM + 'attack/heavy/', 'heavy_explosion_attack_3-{:03d}', 3),
    ('ex_attack_far', EXM + 'attack/heavy/', 'heavy_explosion_attack_3-{:03d}_far', 3),
    ('ex_body', MERGE, 'expl_body_med-{:03d}', 3),
    ('art_attack', MERGE, 'art_attack-{:03d}', 3), ('art_attack_far', MERGE, 'art_attack-{:03d}_far', 3),
    ('art_body', MERGE, 'art_body-{:03d}', 3),
    ('bomb_body', EXM + 'art/slow/bombs_add/', 'slow_attack_big-{:03d}', 3),
    ('bomb_far', MERGE, 'bomb_body_heavy-{:03d}', 3),
    ('debris_small', DEBRIS, 'dirt_debris_small-{:03d}', 2), ('debris_med', DEBRIS, 'dirt_debris_med-{:03d}', 2),
    ('debris_bomb', DEBRIS, 'bomb_debirs-{:03d}', 2),
    ('ex_tail', EXM + 'tails/', 'tails-{:03d}', 3),
    ('ex_sub', 'tanks/explosions/sub_sweetners/', 'explosion_rumble_{:02d}', 3),
    # the rounds going by and coming down (sound_event.h kPassBullet .. kIncoming): shell_whiz, bullet_passby,
    # art_shells_incoming, bombs_incoming
    ('whiz', 'tanks/impacts/', 'shell_whiz_{:02d}', 3),
    ('crack_bullet', PASSBY + 'crack/', 'bullet_crack-{:03d}', 4),
    ('crack_small', PASSBY + 'crack/', 'shell_small_crack-{:03d}', 3),
    ('crack_mid', PASSBY + 'crack/', 'shell_mid_crack-{:03d}', 3),
    ('crack_big', PASSBY + 'crack/', 'shell_big_crack-{:03d}', 2),
    ('bypass_bullet', PASSBY, 'bullet_bypass-{:03d}', 4),
    ('bypass_big', PASSBY, 'projectile_big_bypass-{:03d}', 2),
    ('whiz_small', PASSBY, 'projectile_small_bypass_whiz-{:03d}', 3),
    ('whiz_med', PASSBY, 'projectile_med_bypass_whiz-{:03d}', 3),
    ('whiz_big', PASSBY, 'projectile_big_bypass_whiz-{:03d}', 3),
    ('incoming_art', MERGE, 'art_shell_incoming-{:03d}', 3),
    ('incoming_bomb', MERGE, 'bombs_incoming-{:03d}', 2),
    # the turret and the loader (jetaudio.cpp kWtFor: the vehicles' kClipTurret .. kClipShellRam): turret_turn_loop,
    # turret_klank, cannon_reloading
    ('turret_loop', TURRET, 'turret_turn_loop_{}', 1), ('turret_stop', TURRET, 'turret_klank_{:02d}', 3),
    ('reload_open', RELOAD, 'reloading_open-{:03d}', 4), ('reload_eject', RELOAD, 'reloading_eject_shell-{:03d}', 4),
    ('reload_insert', RELOAD, 'reloading_insert_projectile-{:03d}', 4), ('reload_close', RELOAD, 'reloading_close-{:03d}', 4),
    # the rounds hitting armour (sound_event.h kHitBullet .. kRico): hit_mgun, hit_small_unpierce, hit_big_unpierce,
    # hit_big, hit_aphe_big(_unpierce), rico_mgun, rico (tank_hit_big 1 and 3 peak over the set's peak: one gain for the set
    # would turn every other file down, so they are left out)
    ('hit_bullet', HITS, 'tank_hit_bullet-{:03d}', 6), ('hit_small', HITS, 'tank_hit_small-{:03d}', 3),
    ('hit_mid', HITS, 'tank_hit_mid-{:03d}', 3), ('hit_big', HITS, 'tank_hit_big-{:03d}', (2, 4)),
    ('hit_pierce', HITS, 'tank_hit_pierce-{:03d}', 3),
    ('dull_small', HITS + 'adds/', 'hit_dull_small-{:03d}', 3), ('dull_med', HITS + 'adds/', 'hit_dull_med-{:03d}', 3),
    ('hit_small_far', 'tanks/impacts/hit_far/', 'hit_small_far-{:03d}', 3),
    ('hit_med_far', 'tanks/impacts/hit_far/', 'hit_med_far-{:03d}', 3),
    ('hit_big_far', 'tanks/impacts/hit_far/', 'hit_big_far-{:03d}', 3),
    ('rico_hit', 'tanks/impacts/hit_rico/', 'rico_hit-{:03d}', 3), ('rico_whiz', 'tanks/impacts/hit_rico/', 'rico_whiz-{:03d}', 3),
    ('bullet_whiz', 'tanks/effects/projectile_whiz_rico/', 'bullet_whiz-{:03d}', 4),
)
SOURCES = {n: [d + f.format(i) + '.wav' for i in (k if isinstance(k, tuple) else range(1, k + 1))] for n, d, f, k in _SPEC}
# Mixed down to mono: the low layers (the LFE's alone), the far layers (a point that far off) and the rounds going by (one
# point placed where the round passes: the recordings' own pans are not where it passes here), the turret and the loader (one
# point on the vehicle; the loop's voice is a mono one).
MONO = {'sub', 'ex_sub', 'xs_far', 'ex_attack_far', 'art_attack_far', 'bomb_far', 'whiz', 'crack_bullet', 'crack_small',
        'crack_mid', 'crack_big', 'bypass_bullet', 'bypass_big', 'whiz_small', 'whiz_med', 'whiz_big', 'turret_loop',
        'turret_stop', 'reload_open', 'reload_eject', 'reload_insert', 'reload_close', 'hit_bullet', 'hit_small', 'hit_mid',
        'hit_big', 'hit_pierce', 'dull_small', 'dull_med', 'hit_small_far', 'hit_med_far', 'hit_big_far', 'rico_hit',
        'rico_whiz', 'bullet_whiz'}
# Loops: kept whole (no tail cut, no fade: the end runs back into the start).
LOOPS = {'turret_loop'}
MAX_SECONDS = {'close_150': 5.0, 'tail_heavy': 7.0, 'crack_big': 2.5, 'bypass_big': 2.0, 'hit_small': 2.0, 'hit_mid': 2.0,
               'hit_big': 2.5, 'hit_pierce': 2.0, 'hit_small_far': 2.0, 'hit_med_far': 2.0, 'hit_big_far': 2.5,
               'rico_hit': 1.5, 'rico_whiz': 2.0}


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


def finish(y, gain: float, loop: bool = False):
    import numpy as np
    y = y * gain
    if loop:
        return y
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
            done[f'{name}_{i}.wav'] = (name, rel, process(path, name in MONO, MAX_SECONDS.get(name, 0.0)))
    peak = max(float(np.abs(y).max()) for _, _, y in done.values())
    gain = min(1.0, PEAK_LIMIT / peak)
    os.makedirs(out, exist_ok=True)
    for stale in os.listdir(out):
        if stale.endswith('.wav') and stale not in done:
            os.remove(os.path.join(out, stale))
    lines, total = [], 0
    for fn, (name, rel, y) in done.items():
        z = finish(y, gain, name in LOOPS)
        write_wav(os.path.join(out, fn), z)
        size = os.path.getsize(os.path.join(out, fn))
        total += size
        lines.append(f'| {fn} | {rel} | {z.shape[1]} | {len(z) / RATE:.3f} | {size} |')
    head = (
        '# War Thunder gun, explosion and passby sounds (assets/sound/wt)\n\n'
        f'Source: {UPSTREAM} , commit `{COMMIT}`.\n'
        'The upstream Readme says the project "allows you to create sound modifications" and that using the content means\n'
        'agreeing to the War Thunder contribution agreement (https://live.warthunder.com/contribution_agreement/?lang=en).\n'
        'The repository owner allowed committing the processed clips; the terms are those of the upstream repository.\n'
        'The upstream assets are already vorbis-compressed (ogg renamed to .wav); they are decoded once here and written as\n'
        '16-bit PCM.\n\n'
        'Made by `tools/wt_sound.py` (clip names and variant counts: src/wt_clips.h CREW_WT_CLIPS). Per file: channels kept\n'
        f'({", ".join(f"`{n}`" for n in sorted(MONO))}: mixed to mono), resampled to {RATE} Hz, 20 Hz high-pass, no\n'
        f'per-file levelling or normalising, start kept, tail under -60 dBFS cut with a {int(TAIL_FADE * 1000)} ms fade-out\n'
        f'(loops kept whole: {", ".join(f"`{n}`" for n in sorted(LOOPS))});\n'
        f'cut to a length (300 ms fade-out): {", ".join(f"{n} {t} s" for n, t in MAX_SECONDS.items())}.\n'
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
