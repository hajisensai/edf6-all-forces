"""War Thunder gun, explosion and passby sounds for the plugin's WT sounds (src/wt_clips.h CREW_WT_CLIPS), cut from
Gaijin's official FMOD project for modders (GaijinEntertainment/fmod_studio_warthunder_for_modders, see COMMIT).
The processed WAVs and assets/sound/wt/SOURCE.md are committed (the repository owner allowed it); the installer puts them in
Mods/Plugins/EDF6VehicleCrew_wt/<name>_<i>.wav (i from 1), which is where the plugin reads them.

  python -I tools/wt_sound.py --src <extracted gaijin_fmod_project> --engine-src <extracted banks> [--out assets/sound/wt]
  python -I tools/wt_sound.py --check [--out assets/sound/wt]     tables and committed files agree (needs no source)

The engines (ENGINES, src/sound_engine.h) are not among the modder project's assets: they are War Thunder's own banks
(sound/aircraft_engines.assets.bank, sound/tanks_engines.assets.bank), each stream decoded by vgmstream to
<engine-src>/<bank>.assets.bank/<subsong, 4 digits>_<stream name>.wav; the project's Metadata says which stream is which
event's file (an AudioFile's assetPath and length against the bank's stream name and length, docs/feature-wt-sound.md).

The source is untrusted downloaded data: run with -I and pass the path as an argument.
Each file: 16-bit PCM read, channels kept (the MONO set: mixed down to mono), resampled to 48 kHz, 20 Hz high-pass. No per-file
levelling or normalising (the clips' relative levels are what the FMOD events' dB values assume); when the whole set's
peak is over 0.98 full scale every file is scaled by the same gain. The start is kept (layers of one event line up by
their starts); the tail under -60 dBFS is cut, with a 20 ms fade-out; a clip with a maximum length is cut to it with a
300 ms fade-out. Output: 16-bit PCM WAV, never lossy-coded again (the source is already vorbis).
The engines: every one mixed to mono, the same resampling and high-pass (both by FFT over the whole file: a loop's end
still runs into its start), loops kept whole but their end cross-faded into their start first (loop_seam: several
of the bank's loops are cut at round lengths and jump there), one gain of their own (ENGINE_PEAK: the engines' levels are their events',
not set against the guns').
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
MAX_MB = 120           # the whole set (the guns' about 72 MiB, the engines' under ENGINE_MAX_MB)
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

# The engines (src/sound_engine.h): (name, bank, ((subsong, stream name), ...)), an entry a variant. Jets: Master/aircraft/
# engines/engine05_ej200_WEP (the listener's own), engine05_ej200_WEP_ai and engine_jet_far_ai (the others'). Ground:
# Master/ground/engines/abrams (gas turbine), leo2a5 (diesel: the strv121 recordings and diesel_hiss), m8 (petrol),
# tracks_heavy and tracks_wheels, and their ai/ events. A MultiSound loop of several recordings keeps its first (the size).
AIR, TANK = 'aircraft_engines', 'tanks_engines'
ENGINES = (
    ('jet_idle_rear', AIR, ((1050, 'typhoon_idle_rear'),)), ('jet_max_rear', AIR, ((1202, 'typhoon_max_rear'),)),
    ('jet_idle_front', AIR, ((993, 'typhoon_idle_front'),)), ('jet_max_front', AIR, ((1031, 'typhoon_max_front'),)),
    ('jet_ab', AIR, ((1259, 'typhoon_afterburner'),)),
    ('jet_ab_start', AIR, ((1299, 'afterburner_start-001'), (800, 'afterburner_start-002'), (166, 'afterburner_start-003'))),
    ('jet_turb_02', AIR, ((1207, 'turbine_04_N1_12800_02'),)), ('jet_turb_04', AIR, ((408, 'turbine_04_N1_12800_04'),)),
    ('jet_turb_06', AIR, ((316, 'turbine_04_N1_12800_06'),)), ('jet_turb_08', AIR, ((1117, 'turbine_04_N1_12800_08'),)),
    ('jet_turb_085', AIR, ((119, 'turbine_06_N1_8867_085'),)),
    ('jet_far_low', AIR, ((354, 'jet_low_far'),)), ('jet_far', AIR, ((624, 'jet_far_loop-001'),)),
    ('jet_approach', AIR, ((709, 'jet-001'),)),
    ('gt_00', TANK, ((390, 'engine_00'),)), ('gt_33', TANK, ((2577, 'engine_33'),)), ('gt_66', TANK, ((421, 'engine_66'),)),
    ('gt_99', TANK, ((810, 'engine_99'),)), ('gt_00_load', TANK, ((211, 'engine_00_load'),)),
    ('gt_33_load', TANK, ((2496, 'engine_33_load'),)), ('gt_66_load', TANK, ((2795, 'engine_66_load'),)),
    ('gt_99_load', TANK, ((1810, 'engine_99_load'),)), ('gt_dist', TANK, ((33, 'abrams_dist-001'),)),
    ('di_00', TANK, ((1542, 'strv121_engine_00_exterior'),)), ('di_33', TANK, ((1937, 'strv121_engine_33_exterior'),)),
    ('di_66', TANK, ((3106, 'strv121_engine_66_exterior'),)),
    ('di_33_load', TANK, ((892, 'strv121_engine_33_load_exterior'),)),
    ('di_66_load', TANK, ((1818, 'strv121_engine_66_load_exterior'),)),
    ('di_99_load', TANK, ((3113, 'strv121_engine_99_load_exterior'),)),
    ('di_hiss_idle', TANK, ((2641, 'diesel_hiss_idle'),)), ('di_hiss_low', TANK, ((2022, 'diesel_hiss_low'),)),
    ('di_hiss_high', TANK, ((175, 'diesel_hiss_high'),)),
    ('di_switch', TANK, ((3063, 'strv121_engine_loadswitch_exterior-001'), (3130, 'strv121_engine_loadswitch_exterior-002'),
                         (2792, 'strv121_engine_loadswitch_exterior-003'))),
    ('di_hiss_load', TANK, ((497, 'diesel_hiss_load-001'), (1970, 'diesel_hiss_load-002'))),
    ('di_off', TANK, ((356, 'engine_turbine_off-001'), (1561, 'engine_turbine_off-002'))),
    ('pe_00', TANK, ((2026, 'engine_00'),)), ('pe_33', TANK, ((416, 'engine_33'),)), ('pe_66', TANK, ((1325, 'engine_66'),)),
    ('pe_99', TANK, ((1589, 'engine_99'),)), ('pe_33_load', TANK, ((1566, 'engine_33_load'),)),
    ('pe_66_load', TANK, ((1136, 'engine_66_load'),)), ('pe_99_load', TANK, ((3088, 'engine_99_load'),)),
    ('pe_revv', TANK, ((1428, 'engine_revv-001'), (3038, 'engine_revv-002'), (2617, 'engine_revv-003'))),
    ('pe_release', TANK, ((42, 'engine_release-001'), (1628, 'engine_release-002'), (2635, 'engine_release-003'))),
    ('tr_33', TANK, ((266, 'tracks_33'),)), ('tr_66', TANK, ((2185, 'tracks_66'),)), ('tr_99', TANK, ((2729, 'tracks_99'),)),
    ('tr_stop', TANK, ((2650, 'tracks_stop-001'), (694, 'tracks_stop-002'), (741, 'tracks_stop-003'))),
    ('su_33', TANK, ((1546, 'suspension_33'),)), ('su_66', TANK, ((1152, 'suspension_66'),)),
    ('su_99', TANK, ((1791, 'suspension_99'),)),
    ('su_load', TANK, ((2250, 'suspension_load-001'), (1897, 'suspension_load-002'))),
)
ENGINE_SOURCES = {n: [f'{bank}.assets.bank/{sub:04d}_{stream}.wav' for sub, stream in v] for n, bank, v in ENGINES}
# The engines' one-shots (the rest are loops).
ENGINE_SHOTS = {'jet_ab_start', 'di_switch', 'di_hiss_load', 'di_off', 'pe_revv', 'pe_release', 'tr_stop', 'su_load'}
ENGINE_LOOPS = set(ENGINE_SOURCES) - ENGINE_SHOTS
ENGINE_PEAK = 0.9      # the engines' set scaled so its loudest file peaks here (one gain for them all)
ENGINE_MAX_MB = 40     # the engines' share of the size limit
LOOP_FADE = 0.1        # s: an engine loop's end cross-faded into its start (loop_seam)
ALL_SOURCES = {**SOURCES, **ENGINE_SOURCES}


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
    have = {n: len(v) for n, v in ALL_SOURCES.items()}
    if want != have:
        diff = sorted(set(want) ^ set(have)) + sorted(n for n in want if n in have and want[n] != have[n])
        raise SystemExit(f'src/wt_clips.h and the source table of tools/wt_sound.py differ: {diff}')
    return want


def out_names() -> list[str]:
    return [f'{n}_{i}.wav' for n, v in ALL_SOURCES.items() for i in range(1, len(v) + 1)]


def is_mono(name: str) -> bool:
    """Whether the group `name` (a file's name without _<i>.wav) is written in mono."""
    return name in MONO or name in ENGINE_SOURCES


def read_wav(path: str):
    import numpy as np
    with wave.open(path, 'rb') as w:
        ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if width != 2:
        raise SystemExit(f'{path}: {width * 8}-bit, expected 16-bit PCM')
    x = np.frombuffer(raw, '<i2').astype(np.float64) / 32768
    return x.reshape(-1, ch), rate


def loop_seam(x, fade: int):
    """A recording made a seamless loop: its last `fade` frames cross-faded (equal power: the engines and tracks are
    broadband) into its first, the rest of the end dropped, so its new last frame runs into its first. Several of the
    bank's loops are cut at round lengths (7.5 s, 8 s) and jump at the end; FMOD plays them whole and clicks."""
    import numpy as np
    t = (np.arange(fade) + 0.5) / fade
    y = x[:len(x) - fade].copy()
    y[:fade] = x[:fade] * np.sin(t * np.pi / 2)[:, None] + x[len(x) - fade:] * np.cos(t * np.pi / 2)[:, None]
    return y


def process(path: str, mono: bool, max_seconds: float, loop: bool = False):
    """Float samples (frames, channels) at 48 kHz, 20 Hz high-passed, cut to max_seconds with a fade; no gain yet. An
    engine loop is made seamless first (loop_seam: the FFT resampling and high-pass treat a file as one period)."""
    import numpy as np
    sys.path.insert(0, HERE)
    import sound_pack
    x, rate = read_wav(path)
    if mono and x.shape[1] > 1:
        x = x.mean(axis=1, keepdims=True)
    if loop:
        x = loop_seam(x, int(LOOP_FADE * rate))
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


def _read_all(root: str, table: dict, mono, label: str) -> dict:
    done = {}
    for name, files in table.items():
        for i, rel in enumerate(files, 1):
            path = os.path.join(root, *rel.split('/'))
            if not os.path.isfile(path):
                raise SystemExit(f'missing {label} source: {rel}')
            done[f'{name}_{i}.wav'] = (name, rel, process(path, mono(name), MAX_SECONDS.get(name, 0.0),
                                                          name in ENGINE_LOOPS))
    return done


def build(src: str, engine_src: str, out: str) -> None:
    import numpy as np
    validate_tables()
    guns = _read_all(src, SOURCES, lambda n: n in MONO, 'gun')
    engines = _read_all(engine_src, ENGINE_SOURCES, lambda n: True, 'engine')
    peak = max(float(np.abs(y).max()) for _, _, y in guns.values())
    gain = min(1.0, PEAK_LIMIT / peak)
    engine_peak = max(float(np.abs(y).max()) for _, _, y in engines.values())
    engine_gain = ENGINE_PEAK / engine_peak
    done = {**guns, **engines}
    os.makedirs(out, exist_ok=True)
    for stale in os.listdir(out):
        if stale.endswith('.wav') and stale not in done:
            os.remove(os.path.join(out, stale))
    lines, engine_lines, total, engine_total = [], [], 0, 0
    for fn, (name, rel, y) in done.items():
        engine = name in ENGINE_SOURCES
        z = finish(y, engine_gain if engine else gain, name in (ENGINE_LOOPS if engine else LOOPS))
        write_wav(os.path.join(out, fn), z)
        size = os.path.getsize(os.path.join(out, fn))
        total += size
        row = f'| {fn} | {rel} | {z.shape[1]} | {len(z) / RATE:.3f} | {size} |'
        if engine:
            engine_total += size
            engine_lines.append(row)
        else:
            lines.append(row)
    head = (
        '# War Thunder gun, explosion, passby and engine sounds (assets/sound/wt)\n\n'
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
    engine_head = (
        '\n## The engines (src/sound_engine.h)\n\n'
        'Source: War Thunder\'s own sound banks `sound/aircraft_engines.assets.bank` and `sound/tanks_engines.assets.bank`\n'
        '(the streams the modder project\'s events above name as their files: each AudioFile\'s assetPath and length matched\n'
        'to the bank\'s stream name and length), decoded by vgmstream; under the same terms as the project they belong to.\n'
        'The source column: `<bank>/<subsong>_<stream name>`. Every file mixed to mono, resampled to '
        f'{RATE} Hz and 20 Hz high-passed by FFT over the whole file (a loop\'s end still runs into its start), loops kept\n'
        f'whole, each made seamless by a {int(LOOP_FADE * 1000)} ms equal-power cross-fade of its end into its start\n'
        f'(one-shots: {", ".join(f"`{n}`" for n in sorted(ENGINE_SHOTS))}: their tails cut as above).\n'
        f'One gain for the engines: **{engine_gain:.6f}** (their peak before gain {engine_peak:.6f}, after it {ENGINE_PEAK}).\n\n'
        f'Files: {len(engines)}, {engine_total} bytes ({engine_total / 1048576:.2f} MiB, limit {ENGINE_MAX_MB} MiB).\n\n'
        '| file | source | channels | seconds | bytes |\n|---|---|---|---|---|\n')
    with open(os.path.join(out, 'SOURCE.md'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(head + '\n'.join(lines) + '\n' + engine_head + '\n'.join(engine_lines) + '\n')
    print(f'{len(done)} files, {total} bytes ({total / 1048576:.2f} MiB), gain {gain:.6f}; engines {len(engines)} files, '
          f'{engine_total} bytes ({engine_total / 1048576:.2f} MiB), gain {engine_gain:.6f}')
    if total > MAX_MB * 1048576:
        raise SystemExit(f'over the {MAX_MB} MB limit')
    if engine_total > ENGINE_MAX_MB * 1048576:
        raise SystemExit(f'the engines over their {ENGINE_MAX_MB} MB limit')


def seam(path: str, edge: int = 4800) -> tuple[int, int]:
    """A loop's jump from its last sample to its first, and the largest step between samples within `edge` of the seam
    (16-bit units): a seamless loop's jump is no bigger than its own steps there."""
    import array
    with wave.open(path, 'rb') as w:
        n = w.getnframes()
        head = array.array('h', w.readframes(edge))
        w.setpos(max(0, n - edge))
        tail = array.array('h', w.readframes(edge))
    if sys.byteorder != 'little':
        head.byteswap()
        tail.byteswap()
    steps = [abs(b - a) for x in (head, tail) for a, b in zip(x, x[1:])]
    return abs(head[0] - tail[-1]), max(steps) if steps else 0


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
    total = engine_total = 0
    for f in sorted(names & have):
        p = os.path.join(out, f)
        group = f.rsplit('_', 1)[0]
        size = os.path.getsize(p)
        total += size
        if group in ENGINE_SOURCES:
            engine_total += size
        if f'| {f} |' not in text:
            errors.append(f'SOURCE.md does not list {f}')
        with wave.open(p, 'rb') as w:
            ch, width, rate = w.getnchannels(), w.getsampwidth(), w.getframerate()
        if width != 2 or rate != RATE or ch not in (1, 2) or (is_mono(group) and ch != 1):
            errors.append(f'{f}: {ch} ch, {width * 8}-bit, {rate} Hz')
        elif group in ENGINE_LOOPS:
            jump, step = seam(p)
            if jump > step:
                errors.append(f'{f}: the loop jumps {jump} at its seam, its steps there at most {step}')
    if total > MAX_MB * 1048576:
        errors.append(f'total {total} bytes over {MAX_MB} MB')
    if engine_total > ENGINE_MAX_MB * 1048576:
        errors.append(f'the engines {engine_total} bytes over {ENGINE_MAX_MB} MB')
    return errors


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--src')
    ap.add_argument('--engine-src')
    ap.add_argument('--out', default=DEFAULT_OUT)
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args(argv)
    if a.check:
        errors = check(a.out)
        for e in errors:
            print(e)
        return 1 if errors else 0
    if not a.src or not a.engine_src:
        ap.error('--src and --engine-src are required unless --check')
    build(a.src, a.engine_src, a.out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
