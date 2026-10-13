"""The plugin's vehicle / jet / Sazabi sounds from recordings instead of the ones it makes itself (2026-10-12 user:
「修改的音效重新修改，音效太烂了。可以使用 00_win32_audio.zip 这个」).

The plugin already takes a recording for any of its sounds: <dll name>_veh_<clip>.wav next to the DLL (src/jetaudio.cpp
MakeClipsNow, kClipName) and <dll name>_jet.wav for the jets' roar (Start). This tool cuts, pitches, layers and loops the
recordings MAPPING names out of a sound archive the player has (read straight from the zip: nothing in it is extracted
or run), levels each like the clip it replaces and writes those WAVs. The repository is public and the archive is
another game's sounds, so only this recipe is kept here, never the sounds: the player builds them from their own copy.

  python tools/sound_pack.py build ZIP OUT [--reference DIR] [--skip CLIP ...]
      OUT/EDF6VehicleCrew_veh_<clip>.wav, OUT/EDF6VehicleCrew_jet.wav and OUT/sound_pack.json (each clip's sources,
      their SHA-256, length, peak and loudness against the plugin's own). --reference: vsound_check's WAVs (default
      %TEMP%/edf6_vsound_check; `cmake --build build --target vsound_check && build/vsound_check.exe` writes them);
      without them each clip is levelled to the plugin's peak for it only.
  python tools/sound_pack.py check OUT          every WAV as the plugin reads it (ReadWav), the loops seamless
  python tools/sound_pack.py install OUT GAME   copy them into GAME/Mods/Plugins (refused while EDF6.exe runs)
  python tools/sound_pack.py remove GAME        take this pack's WAVs out again: the plugin makes its own sounds

One clip back to the plugin's own: delete its WAV from Mods/Plugins (or build with --skip CLIP, then install the pack).
An explicit skip removes the old generated WAV from reused OUT and records it in the manifest; install removes that
clip's previous override from the game. Other custom sounds absent from the pack stay as they are.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from dataclasses import dataclass, field
from typing import Optional

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JETAUDIO = os.path.join(ROOT, 'src', 'jetaudio.cpp')
PLUGIN = 'EDF6VehicleCrew'
RATE = 48000          # the plugin's own rate (jetaudio.cpp kRate)
JET = 'jet'           # the jets' roar: <dll>_jet.wav, not a kClipName clip
JET_PEAK = 0.85       # SynthRoar's
JET_LOOP = True
SAMPLES = 'sound/samples/'


@dataclass(frozen=True)
class Layer:
    """One recording in a clip: `src` (under sound/samples/) from `start` to `end` s (None: its end), played `pitch`
    times as fast (higher and shorter), `gain_db` against the clip's first layer, `delay` s in, `lowpass` Hz (0: none)."""
    src: str
    start: float = 0.0
    end: Optional[float] = None
    pitch: float = 1.0
    gain_db: float = 0.0
    delay: float = 0.0
    lowpass: float = 0.0


@dataclass(frozen=True)
class Recipe:
    """A clip made of `layers`. A loop: `loop` s long (a whole number of shots for a gun's burst: `shots` at `rate`
    per second), its end crossfaded into its start. A one-shot: faded out over its last `fade` s, the silence after
    it cut. `why`: the reason it was picked, for the review table (docs/feedback-2026-10-12-sound.md)."""
    clip: str
    layers: tuple[Layer, ...]
    why: str
    loop: float = 0.0
    fade: float = 0.03


# The recordings picked for each clip (docs/feedback-2026-10-12-sound.md has the table and the measures behind it).
# Clips not here keep the sound the plugin makes: the archive has no recording of their kind (tracks, turret motors,
# breech work, cases, the bikes, the Sazabi's blade and funnels' moves, the Q mark's UI cues).
MAPPING: tuple[Recipe, ...] = (
    Recipe(JET, (Layer('engines/fa-18_hornet_steady_ext.wav', 8.0, 16.4),), loop=8.0,
           why='F/A-18 外部稳态录音，43 s 内电平起伏 ±2 dB（最稳的喷气机素材）；取 8 s 做无缝循环'),
    Recipe('engine_heavy_idle', (Layer('engines/engine_tank.wav', 0.5, 6.7, pitch=0.7),), loop=6.0,
           why='坦克引擎录音，8 s 电平几乎恒定（波动 0.20）；降 0.7 倍当怠速，满转时插件再升 2.3 倍≈录音的 1.6 倍'),
    Recipe('engine_heavy_load', (Layer('engines/apc_engine_loop.wav', 0.3, 6.4, pitch=0.7),), loop=5.5,
           why='装甲车引擎循环（文件名即 loop），7.4 s 恒定，频谱比坦克怠速更亮（重心 1.4 kHz），当负荷层'),
    Recipe('engine_light_idle', (Layer('engines/engine_hummer.wav', 0.2, 3.4, pitch=0.85),), loop=3.0,
           why='悍马引擎录音（轻型车），3.5 s 稳定；降 0.85 倍当怠速'),
    Recipe('engine_light_load', (Layer('engines/pickup_engine_02.wav', 0.3, 5.9, pitch=0.85),), loop=5.0,
           why='皮卡引擎录音，6 s 稳定（波动 0.21），当轻型车负荷层'),
    Recipe('mg_burst', (Layer('weapon_fire/4leg_mech_minigun_01_loop.wav', 0.1, 1.5, pitch=12.0 / 12.2),), loop=12 / 12.0,
           why='机枪连射循环素材，实测 12.2 发/秒（插件按 12 发/秒设计）；按 12/12.2 变速后取整 12 发做无缝循环'),
    Recipe('gatling_burst', (Layer('test/minigun_fire.wav'),), loop=0.72,
           why='转管机枪开火录音，0.87 s 电平恒定（波动 0.14）、无单发间隙，正是转管炮的连续嘶吼'),
    Recipe('burst_tail', (Layer('weapon_fire/machine_gun_burst.wav', 0.28, 1.54),), fade=0.2,
           why='机枪点射录音停火后的回响段（0.28 s 起：-19 dB 起逐渐衰减到 -42 dB），当停火回响'),
    Recipe('autocannon', (Layer('weapon_fire/big_gun_01.wav'), Layer('weapon_fire/sniper_shot_01.wav', 0.0, 0.6, gain_db=-8.0)),
           why='大口径单发（起音 75 ms 内 -9 dB、主体在 400 Hz 以下）+ 低 8 dB 的步枪弹爆裂补高频，当 20~40 mm 机炮'),
    Recipe('grenade_shot', (Layer('weapon_fire/mortar_fire_short.wav'),),
           why='迫击炮发射录音：5 ms 起音、0.5 s 的空洞「嗵」、几乎没有 2 kHz 以上（0.3%），就是低膛压榴弹'),
    Recipe('gun_medium_near', (Layer('weapon_fire/105_MM_Cannon_Close.wav'),),
           why='105 mm 坦克炮近距录音（前 100 ms 有 870 Hz 重心的炮口炸裂，随后 2.8 s 低频轰鸣），对应 75~105 mm 档'),
    Recipe('gun_medium_far', (Layer('weapon_fire/105_MM_Cannon_Medium_Distance.wav'),),
           why='同一门 105 mm 炮的中距离录音（没有炸裂，低频滚动 2.8 s）'),
    Recipe('gun_near', (Layer('weapon_fire/105_MM_Cannon_Close.wav', pitch=0.92),
                        Layer('weapon_fire/105_MM_Cannon.wav', pitch=0.92, gain_db=-6.0)),
           why='120 mm：105 mm 近距录音降 0.92 倍（更低更长）+ 低 6 dB 的 105 mm 主录音加厚主体'),
    Recipe('gun_far', (Layer('weapon_fire/105_MM_Cannon_Medium_Distance.wav', pitch=0.92),),
           why='105 mm 中距离录音降 0.92 倍'),
    Recipe('howitzer_near', (Layer('weapon_fire/105_MM_Cannon_Close.wav', pitch=0.8),
                             Layer('explosions/explosion_generic_large_close_01_mono.wav', gain_db=-10.0, delay=0.03)),
           why='155 mm：105 mm 近距录音降 0.8 倍 + 低 10 dB 的大型近爆（20 ms 起音、1.5 s 衰减）当大装药的冲击波'),
    Recipe('howitzer_far', (Layer('weapon_fire/105_MM_Cannon_Medium_Distance.wav', pitch=0.78),),
           why='105 mm 中距离录音降 0.78 倍'),
    Recipe('gun_heavy_near', (Layer('weapon_fire/105_MM_Cannon.wav', pitch=0.72),
                              Layer('explosions/explosion_generic_large_close_02_mono.wav', gain_db=-8.0, delay=0.02)),
           why='Titan 主炮：105 mm 主录音（主体最厚，-3 dB）降 0.72 倍 + 低 8 dB 的大型近爆'),
    Recipe('gun_heavy_far', (Layer('weapon_fire/105_MM_Cannon_Medium_Distance.wav', pitch=0.7),),
           why='105 mm 中距离录音降 0.7 倍'),
    Recipe('railgun_shot', (Layer('weapon_fire/railgun_fire.wav'),),
           why='素材自带的电磁炮：前 0.2 s 470 Hz 的主体，随后电弧从 0.9 kHz 升到 4.6 kHz，没有火药轰鸣'),
    Recipe('railgun_far', (Layer('weapon_fire/railgun_fire.wav', lowpass=1500.0),
                           Layer('weapon_fire/railgun_tower_shot_01.wav', gain_db=-6.0, lowpass=1500.0)),
           why='同一发电磁炮滤到 1.5 kHz 以下 + 低 6 dB 的炮塔电磁炮低频轰响，远处听不到电弧'),
    Recipe('railgun_charge', (Layer('weapon_fire/charging_player_weapon.wav', 2.0, 3.5),), fade=0.05,
           why='武器充能录音的 2.0~3.5 s：啸声重心从 2.2 kHz 一路升到 4.2 kHz，正好 1.5 s（插件在就绪前 90 帧播）；'
               'charging_power 只是 130 Hz 的低频嗡声，不像电容充电'),
    Recipe('railgun_ready', (Layer('misc/beep_tech_05.wav', 0.0, 0.42),), fade=0.05,
           why='科技提示音的第一声（0.42 s），当电容充满'),
    Recipe('missile_launch', (Layer('weapon_fire/rocket_fire.wav'),),
           why='火箭发射录音：点火后 0.3 s 内重心从 0.5 kHz 升到 2.6 kHz 的喷射声，1.1 s'),
    Recipe('rocket_rail_launch', (Layer('weapon_fire/rocket_fire_fast.wav'),),
           why='快速火箭发射录音：比导弹短（0.3 s 内衰减 20 dB）、更干脆，对应喀秋莎每根导轨一声'),
    Recipe('sazabi_beam_shot', (Layer('weapon_fire/plasma_fire_8.wav'),),
           why='等离子射击：第一个 100 ms 重心 3.7 kHz 的电光，随后 1.5 kHz 主体和低频尾音'),
    Recipe('sazabi_beam_hit', (Layer('weapon_hit/bullet_hit_plasma_01.wav'),),
           why='等离子命中：4.2 kHz 的灼烧爆裂 + 170 Hz 闷响，0.6 s（alien_laser_enemy_hit 实测是 5 s 持续嘶声，不用）'),
    Recipe('sazabi_cannon_shot', (Layer('weapon_fire/railgun_tower_shot_01.wav', pitch=0.85),
                                  Layer('weapon_fire/plasma_fire_8.wav', pitch=0.7, gain_db=-4.0)),
           why='胸部扩散炮：电磁炮塔的重型轰响降 0.85 倍 + 降 0.7 倍的光束电光，比步枪大得多'),
    Recipe('sazabi_funnel_shot', (Layer('weapon_fire/active_armor_laser_02.wav'),),
           why='细激光：0.2 s、重心 6~9 kHz，几乎没有 200 Hz 以下，比步枪细很多（浮游炮）'),
    Recipe('sazabi_footstep', (Layer('machines/mech_stomp.wav'),),
           why='机甲踏步录音：撞击的 1.4 kHz 金属敲击后接 50 Hz 的低频余震，1.7 s'),
    Recipe('sazabi_land', (Layer('machines/mech_stomp2.wav'),),
           why='另一段更长的机甲踏步（2.2 s，中段还有金属件的余振），当落地'),
    Recipe('sazabi_dash', (Layer('weapon_fire/power_wooshes_01.wav'),),
           why='能量冲刺「呼」声：0.1 s 升起、0.5 s 内衰减 20 dB'),
)


# --- The plugin's clip table (src/jetaudio.cpp is the one place it is written) ---

def _array(text: str, name: str) -> list[str]:
    m = re.search(name + r'\[kClipCount\]=\{(.*?)\};', text, re.S)
    if not m:
        raise SystemExit(f'{JETAUDIO}: no {name}[kClipCount] table')
    return [s.strip() for s in m.group(1).split(',') if s.strip()]


def clip_table(path: str = JETAUDIO) -> dict[str, tuple[bool, float]]:
    """kClipName -> (kClipLoops, kClipPeak), plus the jets' roar."""
    with open(path, encoding='utf-8') as f:
        text = f.read()
    names = [re.fullmatch(r'L"(\w+)"', s).group(1) for s in _array(text, 'kClipName')]
    loops = [s == 'true' for s in _array(text, 'kClipLoops')]
    peaks = [float(s.rstrip('f')) for s in _array(text, 'kClipPeak')]
    if not len(names) == len(loops) == len(peaks):
        raise SystemExit(f'{JETAUDIO}: kClipName / kClipLoops / kClipPeak lengths differ')
    table = {n: (lp, pk) for n, lp, pk in zip(names, loops, peaks)}
    table[JET] = (JET_LOOP, JET_PEAK)
    return table


def file_name(clip: str) -> str:
    return f'{PLUGIN}_jet.wav' if clip == JET else f'{PLUGIN}_veh_{clip}.wav'


# --- Reading and writing WAVs ---

def decode_wav(data: bytes) -> tuple[np.ndarray, int]:
    """Mono float samples and their rate of a PCM 8/16/24/32-bit or float WAV."""
    if len(data) < 12 or data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        raise ValueError('not a WAV')
    at, fmt = 12, None
    while at + 8 <= len(data):
        tag, n = data[at:at + 4], struct.unpack_from('<I', data, at + 4)[0]
        body = data[at + 8:at + 8 + n]
        if tag == b'fmt ':
            code, ch, rate, _, _, bits = struct.unpack_from('<HHIIHH', body)
            if code == 0xFFFE and len(body) >= 26:
                code = struct.unpack_from('<H', body, 24)[0]
            fmt = (code, ch, rate, bits)
        elif tag == b'data' and fmt:
            code, ch, rate, bits = fmt
            if code == 1 and bits == 16:
                x = np.frombuffer(body[:len(body) // 2 * 2], '<i2').astype(np.float64) / 32768
            elif code == 1 and bits == 24:
                b = np.frombuffer(body[:len(body) // 3 * 3], np.uint8).reshape(-1, 3).astype(np.int32)
                v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
                x = np.where(v >= 1 << 23, v - (1 << 24), v).astype(np.float64) / (1 << 23)
            elif code == 1 and bits == 8:
                x = (np.frombuffer(body, np.uint8).astype(np.float64) - 128) / 128
            elif code == 1 and bits == 32:
                x = np.frombuffer(body[:len(body) // 4 * 4], '<i4').astype(np.float64) / 2 ** 31
            elif code == 3 and bits == 32:
                x = np.frombuffer(body[:len(body) // 4 * 4], '<f4').astype(np.float64)
            else:
                raise ValueError(f'unsupported WAV format {code}/{bits} bit')
            return x[:len(x) // ch * ch].reshape(-1, ch).mean(axis=1), rate
        at += 8 + n + (n & 1)
    raise ValueError('no data chunk')


def encode_wav(x: np.ndarray, rate: int = RATE) -> bytes:
    """16-bit PCM mono: what jetaudio.cpp ReadWav takes."""
    pcm = np.clip(np.round(x * 32767), -32768, 32767).astype('<i2').tobytes()
    head = struct.pack('<4sI4s4sIHHIIHH4sI', b'RIFF', 36 + len(pcm), b'WAVE', b'fmt ', 16, 1, 1, rate, rate * 2, 2, 16,
                       b'data', len(pcm))
    return head + pcm


def resample(x: np.ndarray, factor: float) -> np.ndarray:
    """`x` stretched to len(x) * factor samples (band-limited: FFT, zero-padded or truncated)."""
    n = max(1, int(round(len(x) * factor)))
    if n == len(x):
        return x.copy()
    spec = np.fft.rfft(x)
    out = np.zeros(n // 2 + 1, complex)
    k = min(len(spec), len(out))
    out[:k] = spec[:k]
    return np.fft.irfft(out, n) * (n / len(x))


def lowpass(x: np.ndarray, hz: float, rate: int = RATE) -> np.ndarray:
    spec = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1 / rate)
    spec *= 1.0 / np.sqrt(1.0 + (f / hz) ** 8)   # a 4th-order Butterworth's magnitude, no phase shift
    return np.fft.irfft(spec, len(x))


def highpass(x: np.ndarray, hz: float = 20.0, rate: int = RATE) -> np.ndarray:
    """Under `hz` out (a recording's DC and drift; the plugin's own sounds end with the same 20 Hz high-pass)."""
    spec = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1 / rate)
    spec *= 1.0 / np.sqrt(1.0 + (hz / np.maximum(f, 1e-9)) ** 8)
    return np.fft.irfft(spec, len(x))


def short_loudness(x: np.ndarray, rate: int = RATE, window: float = 0.4) -> float:
    """The loudest `window` s of `x` (RMS): how loud a one-shot is heard."""
    n = max(1, int(window * rate))
    if len(x) <= n:
        return float(np.sqrt(np.mean(x * x))) if len(x) else 0.0
    c = np.concatenate(([0.0], np.cumsum(x * x)))
    return float(np.sqrt(np.max(c[n:] - c[:-n]) / n))


def loudness(x: np.ndarray, is_loop: bool) -> float:
    return float(np.sqrt(np.mean(x * x))) if is_loop else short_loudness(x)


# --- Making a clip ---

class Archive:
    """The sound archive, read in place; each member's bytes hashed for the manifest."""

    def __init__(self, path: str) -> None:
        self.zip = zipfile.ZipFile(path)
        self.names = set(self.zip.namelist())
        self.sha: dict[str, str] = {}
        self.cache: dict[str, np.ndarray] = {}

    def read(self, src: str) -> np.ndarray:
        member = SAMPLES + src
        if member not in self.names:
            raise SystemExit(f'the archive has no {member}: is it 00_win32_audio.zip?')
        if member not in self.cache:
            data = self.zip.read(member)
            self.sha[src] = hashlib.sha256(data).hexdigest()
            x, rate = decode_wav(data)
            self.cache[member] = resample(x, RATE / rate) if rate != RATE else x
        return self.cache[member]


def render_layer(a: Archive, layer: Layer) -> np.ndarray:
    x = a.read(layer.src)
    end = len(x) if layer.end is None else min(len(x), int(layer.end * RATE))
    x = x[int(layer.start * RATE):end]
    if layer.pitch != 1.0:
        x = resample(x, 1.0 / layer.pitch)
    if layer.lowpass:
        x = lowpass(x, layer.lowpass)
    x = x * 10 ** (layer.gain_db / 20)
    return np.concatenate((np.zeros(int(layer.delay * RATE)), x))


def mix(parts: list[np.ndarray]) -> np.ndarray:
    out = np.zeros(max(len(p) for p in parts))
    for p in parts:
        out[:len(p)] += p
    return out


def make_loop(x: np.ndarray, seconds: float, fade: float = 0.12) -> np.ndarray:
    """`seconds` of `x` whose end runs into its start: the `fade` s after it crossfaded (equal power) over its first."""
    n, f = int(round(seconds * RATE)), int(fade * RATE)
    slack = int(0.005 * RATE)
    if len(x) < n + slack + f:
        raise SystemExit(f'a {seconds} s loop needs {(n + slack + f) / RATE:.2f} s of recording, it has {len(x) / RATE:.2f} s')
    # The seam is x[n-1] -> x[n] (the crossfade starts the loop on x[n]): put it where the recording steps least,
    # within 5 ms of the length asked for (a gun's burst keeps its whole shots).
    steps = np.abs(np.diff(x[n - slack - 1:n + slack]))
    n = n - slack + int(np.argmin(steps))
    body = x[:n].copy()
    t = np.linspace(0.0, np.pi / 2, f, endpoint=False)
    body[:f] = body[:f] * np.sin(t) + x[n:n + f] * np.cos(t)
    return body - body.mean()


def finish_shot(x: np.ndarray, fade: float) -> np.ndarray:
    """The silence after the sound cut (under -60 dB of its peak), the last `fade` s faded out, a 2 ms fade in."""
    loud = np.nonzero(np.abs(x) > np.max(np.abs(x)) * 1e-3)[0]
    x = x[:loud[-1] + 1] if len(loud) else x
    f = min(len(x), int(fade * RATE))
    x = x.copy()
    if f:
        x[-f:] *= np.cos(np.linspace(0.0, np.pi / 2, f)) ** 2
    i = min(len(x), int(0.002 * RATE))
    x[:i] *= np.linspace(0.0, 1.0, i)
    return x


def level(x: np.ndarray, peak: float, is_loop: bool, reference: Optional[np.ndarray]) -> tuple[np.ndarray, Optional[float]]:
    """`x` as loud as the plugin's own clip (`reference`) but never over `peak`; without one, to `peak`. Returns it and
    how far (dB) it ended up from the reference (None: none)."""
    top = float(np.max(np.abs(x))) or 1.0
    gain = peak / top
    if reference is not None and len(reference):
        want = loudness(reference, is_loop) / (loudness(x, is_loop) or 1e-9)
        gain = min(gain, want)
    y = x * gain
    off = None
    if reference is not None and len(reference):
        off = 20 * np.log10((loudness(y, is_loop) or 1e-9) / (loudness(reference, is_loop) or 1e-9))
    return y, off


def build_clip(a: Archive, r: Recipe, table: dict[str, tuple[bool, float]], reference_dir: Optional[str]) -> tuple[np.ndarray, dict]:
    is_loop, peak = table[r.clip]
    x = highpass(mix([render_layer(a, layer) for layer in r.layers]))
    x = make_loop(x, r.loop) if is_loop else finish_shot(x, r.fade)
    ref = None
    ref_path = os.path.join(reference_dir, r.clip + '.wav') if reference_dir else ''
    if ref_path and os.path.isfile(ref_path):
        with open(ref_path, 'rb') as f:
            ref, ref_rate = decode_wav(f.read())
        ref = resample(ref, RATE / ref_rate) if ref_rate != RATE else ref
    y, off = level(x, peak, is_loop, ref)
    info = {'clip': r.clip, 'file': file_name(r.clip), 'loop': is_loop, 'seconds': round(len(y) / RATE, 3),
            'peak': round(float(np.max(np.abs(y))), 3),
            'vs_plugin_db': None if off is None else round(off, 1),
            'sources': [{'src': layer.src, 'sha256': a.sha[layer.src], 'start': layer.start, 'end': layer.end,
                         'pitch': layer.pitch, 'gain_db': layer.gain_db, 'delay': layer.delay, 'lowpass': layer.lowpass}
                        for layer in r.layers],
            'why': r.why}
    return y, info


def validate_mapping(table: dict[str, tuple[bool, float]]) -> None:
    seen = set()
    for r in MAPPING:
        if r.clip not in table:
            raise SystemExit(f'MAPPING: {r.clip} is not one of the plugin\'s clips (src/jetaudio.cpp kClipName)')
        if r.clip in seen:
            raise SystemExit(f'MAPPING: {r.clip} twice')
        seen.add(r.clip)
        if table[r.clip][0] != (r.loop > 0):
            raise SystemExit(f'MAPPING: {r.clip} is {"a loop" if table[r.clip][0] else "a one-shot"} in the plugin')


def default_reference() -> str:
    return os.path.join(tempfile.gettempdir(), 'edf6_vsound_check')


def build(zip_path: str, out: str, reference_dir: Optional[str], skip: set[str]) -> list[dict]:
    table = clip_table()
    validate_mapping(table)
    unknown = skip - {r.clip for r in MAPPING}
    if unknown:
        raise SystemExit(f'--skip: not in the pack: {", ".join(sorted(unknown))}')
    if reference_dir and not os.path.isdir(reference_dir):
        print(f'no {reference_dir}: levelled to the plugin\'s peaks only (run vsound_check to match its loudness)')
        reference_dir = None
    a = Archive(zip_path)
    os.makedirs(out, exist_ok=True)
    infos = []
    for r in MAPPING:
        if r.clip in skip:
            # OUT may be reused after an earlier full build. Its old override must not survive an explicit skip.
            stale = os.path.join(out, file_name(r.clip))
            if os.path.isfile(stale):
                os.remove(stale)
            continue
        y, info = build_clip(a, r, table, reference_dir)
        with open(os.path.join(out, info['file']), 'wb') as f:
            f.write(encode_wav(y))
        infos.append(info)
        off = '' if info['vs_plugin_db'] is None else f'  {info["vs_plugin_db"]:+.1f} dB vs the plugin\'s'
        print(f'{info["file"]:44s} {info["seconds"]:6.2f} s  peak {info["peak"]:.2f}{off}')
    with open(os.path.join(out, 'sound_pack.json'), 'w', encoding='utf-8') as f:
        json.dump({'archive_sha256': sha256_file(zip_path), 'clips': infos, 'skipped': sorted(skip)}, f,
                  ensure_ascii=False, indent=1)
    return infos


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


# --- Checking what was written, as the plugin will read it ---

def plugin_reads(data: bytes) -> Optional[tuple[int, int]]:
    """jetaudio.cpp ReadWav's verdict: (frames, rate) when it takes the file, None when it falls back to its own."""
    if not 12 <= len(data) < (64 << 20) or data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        return None
    channels = bits = fmt = rate = 0
    at = 12
    while at + 8 <= len(data):
        n = struct.unpack_from('<I', data, at + 4)[0]
        if at + 8 + n > len(data):
            break
        tag, body = data[at:at + 4], data[at + 8:at + 8 + n]
        if tag == b'fmt ' and n >= 16:
            fmt, channels, rate = struct.unpack_from('<HHI', body)
            bits = struct.unpack_from('<H', body, 14)[0]
        elif tag == b'data' and fmt == 1 and bits == 16 and channels in (1, 2):
            frames = n // (2 * channels)
            return (frames, rate) if frames > rate // 10 and 8000 <= rate <= 192000 else None
        at += 8 + n + (n & 1)
    return None


def check(out: str) -> list[str]:
    """What is wrong with OUT's WAVs (empty: nothing)."""
    table = clip_table()
    by_file = {file_name(c): c for c in table}
    errors = []
    found = 0
    for name in sorted(os.listdir(out)):
        if not name.lower().endswith('.wav'):
            continue
        clip = by_file.get(name)
        if clip is None:
            errors.append(f'{name}: the plugin reads no file of this name')
            continue
        found += 1
        with open(os.path.join(out, name), 'rb') as f:
            data = f.read()
        if plugin_reads(data) is None:
            errors.append(f'{name}: the plugin would not read it (16-bit PCM, 1-2 channels, over 0.1 s)')
            continue
        x, _ = decode_wav(data)
        is_loop, peak = table[clip]
        top = float(np.max(np.abs(x)))
        if top > peak + 0.01:
            errors.append(f'{name}: peak {top:.3f} over the plugin\'s {peak}')
        if abs(float(np.mean(x))) > 0.01:
            errors.append(f'{name}: DC offset {float(np.mean(x)):.3f}')
        if is_loop:
            step = float(np.percentile(np.abs(np.diff(x)), 99.9))
            seam = abs(float(x[0] - x[-1]))
            if seam > step:
                errors.append(f'{name}: the loop jumps {seam:.4f} at its seam (its steps: {step:.4f})')
    if not found:
        errors.append(f'{out}: no WAVs of this pack')
    return errors


# --- Into the game and out ---

def game_running() -> bool:
    if os.name != 'nt':
        return False
    out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq EDF6.exe', '/NH'], capture_output=True, text=True).stdout
    return 'EDF6.exe' in out


def pack_files() -> list[str]:
    return [file_name(c) for c in clip_table()]


def install(out: str, game: str) -> None:
    plugins = os.path.join(game, 'Mods', 'Plugins')
    if not os.path.isfile(os.path.join(plugins, PLUGIN + '.dll')):
        raise SystemExit(f'no {PLUGIN}.dll in {plugins}')
    if game_running():
        raise SystemExit('EDF6 is running: the plugin reads the sounds when it starts; quit the game first')
    errors = check(out)
    if errors:
        raise SystemExit('not installed:\n  ' + '\n  '.join(errors))
    # Only explicit skips restore a previous install to synthesis. A missing WAV alone may be a partial pack,
    # and must not remove the player's other custom overrides.
    manifest = os.path.join(out, 'sound_pack.json')
    skipped = []
    if os.path.isfile(manifest):
        with open(manifest, encoding='utf-8') as f:
            skipped = json.load(f).get('skipped', [])
        allowed = {r.clip for r in MAPPING}
        if not isinstance(skipped, list) or any(not isinstance(c, str) or c not in allowed for c in skipped):
            raise SystemExit('not installed: invalid skipped clips in sound_pack.json')
        if any(os.path.isfile(os.path.join(out, file_name(c))) for c in skipped):
            raise SystemExit('not installed: a skipped clip still has a WAV; rebuild the pack')
    for name in pack_files():
        src = os.path.join(out, name)
        if os.path.isfile(src):
            shutil.copyfile(src, os.path.join(plugins, name))
            if sha256_file(src) != sha256_file(os.path.join(plugins, name)):
                raise SystemExit(f'{name}: the copy differs')
            print(f'installed {name}')
    for clip in skipped:
        path = os.path.join(plugins, file_name(clip))
        if os.path.isfile(path):
            os.remove(path)
            print(f'restored plugin sound {clip}')


def remove(game: str) -> None:
    plugins = os.path.join(game, 'Mods', 'Plugins')
    if game_running():
        raise SystemExit('EDF6 is running: quit the game first')
    for name in pack_files():
        path = os.path.join(plugins, name)
        if os.path.isfile(path):
            os.remove(path)
            print(f'removed {name}')


def main(argv: Optional[list[str]] = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    b = sub.add_parser('build')
    b.add_argument('zip')
    b.add_argument('out')
    b.add_argument('--reference', default=default_reference())
    b.add_argument('--skip', nargs='*', default=[])
    c = sub.add_parser('check')
    c.add_argument('out')
    i = sub.add_parser('install')
    i.add_argument('out')
    i.add_argument('game')
    r = sub.add_parser('remove')
    r.add_argument('game')
    a = p.parse_args(argv)
    if a.cmd == 'build':
        build(a.zip, a.out, a.reference, set(a.skip))
        errors = check(a.out)
    elif a.cmd == 'check':
        errors = check(a.out)
    elif a.cmd == 'install':
        install(a.out, a.game)
        return 0
    else:
        remove(a.game)
        return 0
    for e in errors:
        print('FAIL', e)
    print('ok' if not errors else f'{len(errors)} problem(s)')
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
