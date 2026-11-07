"""The report of a tools/perfrun.py measurement: frame times, threads, GPU, and where the game spends its time.

  python tools/perf_report.py RUN_DIR [--top N]     report.md of one run (written into RUN_DIR and printed)
  python tools/perf_report.py --ab RUN_DIR...       the A/B table of several runs (perfrun --ab writes it itself)

RUN_DIR holds what perfrun left: run.json (scenario, DLL, the windows as wall-clock seconds), cpu.txt (rip_sampler with
interval 0: each thread's CPU over the clean window, nothing suspended), stacks.txt (rip_sampler stacks), gpu.csv
(typeperf GPU engines over the clean window) and vehiclecrew.log (the plugin's Debug=1 PERF rows, one per 5 s).
Addresses are EDF.dll RVAs, as in docs/*-re.md.
"""
from __future__ import annotations

import bisect
import collections
import csv
import json
import os
import re
import statistics
import struct
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))

# EDF.dll functions that name a thread (docs/frame-pacing-re.md): its frame loop, the window thread's loop.
SYSTEM_UPDATE = 0x1198DA0
WINDOW_LOOP = 0x1184460
APP_FRAME = 0x705530
# System::update's phases: a sample's return address in it past one of these is in that phase (each the load of the
# phase's name, e.g. lea rdx, "xgs_scene_object_ai_update", or the call starting it; checked by disassembly 2026-11-07).
PHASES = ((0x1198F76, 'setup: draw 0x11978D0'), (0x1198FA9, 'setup2'), (0x11990D4, 'ai_update'),
          (0x119920A, 'pre_update'), (0x1199334, 'havok_update'), (0x11993BA, 'object_update'),
          (0x119953B, 'camera_update'))
PLUGIN_MODULES = ('edf6', 'patcher')
PERF = re.compile(r'\[(\d+):(\d+):(\d+(?:\.\d+)?)\] PERF (\d+) frames: mean ([\d.]+) ms \(\d+ fps\), worst ([\d.]+) ms'
                  r'(?:, (\d+) over 20 ms)?, (\d+) over 33 ms')


class Image:
    """EDF.dll's .pdata (function ranges) and its vtables' slots by class (RTTI), for naming stack frames."""

    def __init__(self, dll: str, classes: bool = True) -> None:
        import pefile
        pe = pefile.PE(dll, fast_load=True)
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION']])
        self.pdata = sorted((e.struct.BeginAddress, e.struct.EndAddress) for e in pe.DIRECTORY_ENTRY_EXCEPTION)
        self.starts = [b for b, _ in self.pdata]
        self.slot_class: dict[int, str] = {}
        if classes:
            self._classes(pe)

    def _classes(self, pe) -> None:
        base = pe.OPTIONAL_HEADER.ImageBase
        img = pe.get_memory_mapped_image()
        secs = {s.Name.rstrip(b'\0').decode(): (s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize)
                for s in pe.sections}
        rd, tx = secs['.rdata'], secs['.text']
        rdata = img[rd[0]:rd[1]]
        # a vtable is preceded by its CompleteObjectLocator pointer (signature 1, then the TypeDescriptor's RVA)
        for off in range(0, len(rdata) - 16, 8):
            col = struct.unpack_from('<Q', rdata, off)[0] - base
            if not rd[0] <= col < rd[1] - 24 or struct.unpack_from('<I', img, col)[0] != 1:
                continue
            td = struct.unpack_from('<I', img, col + 12)[0]
            if not 0 < td < len(img) - 120:
                continue
            name = img[td + 16:td + 120].split(b'\0')[0]
            if not name.startswith(b'.?AV'):
                continue
            label = name[4:].removesuffix(b'@@').decode(errors='replace')
            p, k = off + 8, 0
            while p < len(rdata) - 8:
                f = struct.unpack_from('<Q', rdata, p)[0] - base
                if not tx[0] <= f < tx[1]:
                    break
                self.slot_class.setdefault(f, f'{label}#{k}')
                p += 8
                k += 1

    def start(self, rva: int) -> int | None:
        i = bisect.bisect_right(self.starts, rva) - 1
        return self.pdata[i][0] if i >= 0 and self.pdata[i][0] <= rva < self.pdata[i][1] else None

    def name(self, rva: int | None) -> str:
        if rva is None:
            return '?'
        return f'{rva:#x}' + (f' {self.slot_class[rva]}' if rva in self.slot_class else '')


def frame_rva(frame: str) -> int | None:
    mod, _, off = frame.rpartition('+')
    return int(off, 16) if mod.lower() == 'edf.dll' else None


def module(frame: str) -> str:
    return frame.rpartition('+')[0]


def is_plugin(frame: str) -> bool:
    return module(frame).lower().startswith(PLUGIN_MODULES)


def read_samples(path: str) -> dict[str, list[list[str]]]:
    """rip_sampler's T lines by thread: each a stack, leaf first."""
    out: dict[str, list[list[str]]] = collections.defaultdict(list)
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            p = line.split()
            if len(p) >= 4 and p[0] == 'T':
                out[p[1]].append(p[3:])
    return out


def read_cpu(path: str) -> dict[str, int]:
    """rip_sampler's C lines (interval 0): CPU ms by thread, 'total' the process."""
    out = {}
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            p = line.split()
            if len(p) == 3 and p[0] == 'C':
                out[p[1]] = int(p[2])
    return out


def read_perf(log: str, window: tuple[float, float]) -> list[tuple[float, int, float, float, int | None, int]]:
    """The plugin's PERF rows (each the 5 s before its time) that lie inside `window` (wall-clock seconds): (time of
    day, frames, mean ms, worst ms, over 20 ms or None on an older build, over 33 ms)."""
    def tod(t: float) -> float:
        lt = time.localtime(t)
        return lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec + (t % 1)
    a, b = tod(window[0]), tod(window[1])
    rows = []
    try:
        with open(log, encoding='utf-8', errors='replace') as f:
            lines = f.read().splitlines()
    except OSError:
        return rows
    for line in lines:
        m = PERF.search(line)
        if not m:
            continue
        t = int(m[1]) * 3600 + int(m[2]) * 60 + float(m[3])
        if a + 5 <= t <= b + 0.5:
            rows.append((t, int(m[4]), float(m[5]), float(m[6]), int(m[7]) if m[7] else None, int(m[8])))
    return rows


def frame_summary(rows) -> dict:
    if not rows:
        return {}
    frames = sum(r[1] for r in rows)
    mean = sum(r[1] * r[2] for r in rows) / frames
    over20 = None if any(r[4] is None for r in rows) else sum(r[4] for r in rows)
    return {'rows': len(rows), 'frames': frames, 'mean_ms': mean, 'fps': 1000 / mean,
            'over20_pct': None if over20 is None else over20 / frames * 100, 'over33': sum(r[5] for r in rows),
            'worst_median_ms': statistics.median(r[3] for r in rows), 'worst_max_ms': max(r[3] for r in rows)}


def gpu_summary(path: str) -> dict[str, tuple[float, float]]:
    """typeperf's GPU engine utilization by engine type, as Task Manager counts it: the busiest engine of the type in
    each sample (the counter's instances for one engine summed first), then its mean and 95th percentile in %."""
    try:
        with open(path, newline='', encoding='utf-8', errors='replace') as f:
            rows = list(csv.reader(f))
    except OSError:
        return {}
    if len(rows) < 2:
        return {}

    def engine(h: str) -> tuple[str, str] | None:
        m = re.search(r'(luid_\w+?_phys_\d+_eng_\d+)_engtype_([^)]+)\)', h)
        return (m[2], m[1]) if m else None
    keys = [engine(h) for h in rows[0]]
    per: dict[str, list[float]] = collections.defaultdict(list)
    for row in rows[1:]:
        eng: dict[tuple[str, str], float] = collections.Counter()
        for k, v in zip(keys, row):
            if k:
                try:
                    eng[k] += float(v)
                except ValueError:
                    pass
        busiest: dict[str, float] = {}
        for (kind, _), v in eng.items():
            busiest[kind] = max(busiest.get(kind, 0.0), v)
        for kind, v in busiest.items():
            per[kind].append(v)
    out = {}
    for k, vals in per.items():
        if vals and max(vals) > 0.5:
            vals.sort()
            out[k] = (statistics.mean(vals), vals[min(len(vals) - 1, int(len(vals) * 0.95))])
    return out


def thread_role(img: Image, stacks: list[list[str]]) -> str:
    starts = collections.Counter()
    for st in stacks:
        for s in {img.start(r) for r in map(frame_rva, st) if r is not None}:
            starts[s] += 1
    if starts[SYSTEM_UPDATE] > len(stacks) * 0.2:
        return 'game thread (System::update)'
    if starts[WINDOW_LOOP] > len(stacks) * 0.2:
        return 'window thread'
    leaf = collections.Counter(module(st[0]) for st in stacks).most_common(2)
    return 'leaf ' + ', '.join(f'{m} {c * 100 // len(stacks)}%' for m, c in leaf)


def phases(img: Image, stacks: list[list[str]], top: int) -> list[str]:
    """The game thread by System::update phase, and below each the class whose virtual the phase loop called."""
    ph = collections.Counter()
    below: dict[str, collections.Counter] = collections.defaultdict(collections.Counter)
    for st in stacks:
        rvas = [frame_rva(f) for f in st]
        starts = [img.start(r) if r is not None else None for r in rvas]
        if SYSTEM_UPDATE in starts:
            i = starts.index(SYSTEM_UPDATE)
            ret = rvas[i]
            key = 'setup'
            for a, n in PHASES:
                if ret > a:
                    key = n
        else:
            i = starts.index(APP_FRAME) if APP_FRAME in starts else len(starts)
            key = 'outside System::update' + (f' (App frame ret {rvas[i]:#x})' if i < len(starts) else '')
        ph[key] += 1
        chain = [img.name(s) for s in reversed(starts[:i]) if s is not None][:2]
        below[key][' > '.join(chain) + (' [plugin below]' if any(is_plugin(f) for f in st[:i]) else '')
                   if chain else '(in the phase itself / system code)'] += 1
    n = len(stacks)
    out = []
    for k, v in ph.most_common():
        out.append(f'- {v / n * 100:5.1f}% {k}')
        for c, m in below[k].most_common(top // 2):
            out.append(f'  - {m / n * 100:5.1f}% {c}')
    return out


def thread_detail(img: Image, stacks: list[list[str]], top: int) -> list[str]:
    n = len(stacks)
    selfc = collections.Counter()
    callers: dict[int, collections.Counter] = collections.defaultdict(collections.Counter)
    incl = collections.Counter()
    leaf_via: dict[str, collections.Counter] = collections.defaultdict(collections.Counter)
    plugin = 0
    for st in stacks:
        starts = [img.start(r) if (r := frame_rva(f)) is not None else None for f in st]
        edf = [s for s in starts if s is not None]
        if edf:
            selfc[edf[0]] += 1
            callers[edf[0]][img.name(edf[1]) if len(edf) > 1 else '-'] += 1
        for s in set(edf):
            incl[s] += 1
        if frame_rva(st[0]) is None:
            leaf_via[module(st[0])][' < '.join(img.name(s) for s in edf[:2]) or '(no EDF frame)'] += 1
        plugin += any(is_plugin(f) for f in st)
    out = [f'plugin frames on the stack: {plugin / n * 100:.1f}%', '', 'self (deepest EDF.dll function, system calls '
           'below it included):', '']
    out += [f'- {c / n * 100:5.1f}% {img.name(f)} <- ' + ', '.join(f'{k} {v}' for k, v in callers[f].most_common(3))
            for f, c in selfc.most_common(top)]
    out += ['', 'inclusive:', '']
    out += [f'- {c / n * 100:5.1f}% {img.name(f)}' for f, c in incl.most_common(top)]
    out += ['', 'leaves outside EDF.dll, by the EDF functions that called them:', '']
    for mod, via in sorted(leaf_via.items(), key=lambda kv: -sum(kv[1].values()))[:6]:
        out.append(f'- {sum(via.values()) / n * 100:5.1f}% {mod}: ' +
                   '; '.join(f'{k} {v}' for k, v in via.most_common(3)))
    return out


def edf_dll() -> str:
    import gamedir
    return os.path.join(gamedir.find_or_dev(), 'EDF.dll')


def report(run_dir: str, top: int = 20, img: Image | None = None) -> str:
    with open(os.path.join(run_dir, 'run.json'), encoding='utf-8') as f:
        run = json.load(f)
    cw = tuple(run['windows']['cpu'])
    secs = cw[1] - cw[0]
    out = [f'# {run["scenario"]}' + (f' ({run["variant"]})' if run.get('variant') else ''), '',
           f'DLL {run.get("dll") or "(installed)"} sha256 {run.get("sha256", "?")[:16]}; clean window {secs:.0f} s, '
           f'{run["warm"]:.0f} s into the mission, Debug=1 (the shipped default)' + (f'; stacks {run["windows"]["stacks"][1] - run["windows"]["stacks"][0]:.0f} s'
                                                       if run['windows'].get('stacks') else ''), '']
    fs = frame_summary(read_perf(os.path.join(run_dir, 'vehiclecrew.log'), cw))
    out.append('## Frames (clean window)')
    out.append('')
    if fs:
        out.append(f'{fs["frames"]} frames: mean {fs["mean_ms"]:.2f} ms ({fs["fps"]:.1f} fps), over 20 ms '
                   + (f'{fs["over20_pct"]:.2f}%' if fs['over20_pct'] is not None else 'n/a (older build)')
                   + f', over 33 ms {fs["over33"]}, worst of each 5 s: median {fs["worst_median_ms"]:.1f} ms, '
                   f'max {fs["worst_max_ms"]:.1f} ms')
    else:
        out.append('no PERF rows in the window (Debug=1 not on, or the window too short)')
    cpu = read_cpu(os.path.join(run_dir, 'cpu.txt')) if os.path.exists(os.path.join(run_dir, 'cpu.txt')) else {}
    stacks_path = os.path.join(run_dir, 'stacks.txt')
    samples = read_samples(stacks_path) if os.path.exists(stacks_path) else {}
    if samples and img is None:
        img = Image(run.get('edf_dll') or edf_dll())
    roles = {t: thread_role(img, s) for t, s in samples.items() if len(s) >= 20} if samples else {}
    out += ['', '## Threads (clean window: CPU, % of one core)', '']
    if cpu:
        total = cpu.pop('total', sum(cpu.values()))
        out.append(f'process {total / 1000:.1f} s CPU in {secs:.0f} s ({total / 10 / secs:.0f}% of one core)')
        out.append('')
        for t, ms in sorted(cpu.items(), key=lambda kv: -kv[1])[:10]:
            if ms < secs * 10:   # under 1% of a core
                break
            out.append(f'- {t}: {ms / 10 / secs:5.1f}%  {roles.get(t, "")}')
    gpu = gpu_summary(os.path.join(run_dir, 'gpu.csv'))
    if gpu:
        out += ['', '## GPU engines (utilization: mean, 95th percentile)', '']
        out += [f'- {k}: {m:.0f}% / {p:.0f}%' for k, (m, p) in sorted(gpu.items(), key=lambda kv: -kv[1][0])]
    if cpu:
        busiest = max(cpu.items(), key=lambda kv: kv[1])
        hint = (f'thread {busiest[0]} ({roles.get(busiest[0], "?")}) runs {busiest[1] / 10 / secs:.0f}% of a core: '
                'CPU-bound on it' if busiest[1] / 10 / secs > 85 else
                'no thread is near a full core: the frames wait (GPU, the pacing, or locks); see GPU and the stacks')
        out += ['', f'**Bottleneck hint:** {hint}']
    if samples:
        total_n = sum(len(s) for s in samples.values())
        out += ['', f'## Stacks ({total_n} samples)', '']
        for t in sorted(samples, key=lambda t: -len(samples[t]))[:4]:
            s = samples[t]
            out += [f'### thread {t}: {len(s)} samples ({len(s) / total_n * 100:.0f}%), {roles.get(t, "")}', '']
            if roles.get(t, '').startswith('game thread'):
                out += ['by phase:', ''] + phases(img, s, top) + ['']
            out += thread_detail(img, s, top) + ['']
    text = '\n'.join(out) + '\n'
    with open(os.path.join(run_dir, 'report.md'), 'w', encoding='utf-8') as f:
        f.write(text)
    return text


def ab_table(run_dirs: list[str]) -> str:
    rows = []
    for d in run_dirs:
        try:
            with open(os.path.join(d, 'run.json'), encoding='utf-8') as f:
                run = json.load(f)
        except OSError:
            run = {}
        if 'cpu' not in run.get('windows', {}):
            rows.append((run.get('variant', '?'), os.path.basename(d), {}, None))
            continue
        cw = tuple(run['windows']['cpu'])
        fs = frame_summary(read_perf(os.path.join(d, 'vehiclecrew.log'), cw))
        cpu = read_cpu(os.path.join(d, 'cpu.txt')) if os.path.exists(os.path.join(d, 'cpu.txt')) else {}
        secs = cw[1] - cw[0]
        rows.append((run.get('variant', '?'), os.path.basename(d), fs, cpu.get('total', 0) / 10 / secs if cpu else None))
    out = ['| variant | run | mean ms | over 20 ms | over 33 ms | worst of 5 s (median) | process CPU (% of a core) |',
           '|---|---|---|---|---|---|---|']
    for v, name, fs, c in sorted(rows, key=lambda r: (r[0], r[1])):
        if not fs:
            out.append(f'| {v} | {name} | ' + ('no PERF rows' if c is not None else 'not sampled') +
                       f' | | | | {"" if c is None else f"{c:.0f}"} |')
            continue
        o20 = f'{fs["over20_pct"]:.2f}%' if fs['over20_pct'] is not None else 'n/a'
        out.append(f'| {v} | {name} | {fs["mean_ms"]:.2f} | {o20} | {fs["over33"]} | {fs["worst_median_ms"]:.1f} | '
                   f'{c or 0:.0f} |')
    return '\n'.join(out) + '\n'


def main(argv: list[str]) -> int:
    if not argv or argv[0] in ('-h', '--help'):
        print(__doc__)
        return 1
    if argv[0] == '--ab':
        print(ab_table(argv[1:]))
        return 0
    top = int(argv[argv.index('--top') + 1]) if '--top' in argv else 20
    print(report(argv[0], top))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
