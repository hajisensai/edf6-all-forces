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
# The frame step loop's phases: per step (everything after the draw); the draw and what is outside System::update: per
# frame.
STEP_PHASES = ('setup2', 'ai_update', 'pre_update', 'havok_update', 'object_update', 'camera_update')
# The draw recording's parts (cpu-1): the camera's projection and Umbra, the xgs pool's flush, the rest.
DRAW_PARTS = ((0x118DFF0, 'camera / Umbra 0x118DFF0'), (0x1197090, 'pool flush 0x1197090'))
POOL_WAIT = 0x65F10   # the xgs pool's join: each phase ends waiting on its workers here (docs/reviews cpu-7)
# Thread roles by a function on the stack (docs/frame-pacing-re.md, havok-mt-re.md section 12, aiworld-re.md), the
# first that matches. 0x3E630 last: every xgs thread starts there (the game thread too, M001 2026-11-24), so it only
# says "an xgs thread" when nothing more specific is on the stacks.
ROLES = ((0x130FE20, 'Havok worker (processTaskQueue)'), (0x111B8E0, 'render thread'), (0x1118AF0, 'render thread'),
         (0xF3AA0, 'ai::World thread'), (0xF3E60, 'ai::World thread'), (0x3E630, 'xgs thread (pool worker or other)'))
# Application::Update 0x705530's calls by return address, the lowest of each bucket (docs/unlimited-fps-re.md 2.2).
APP_BUCKETS = ((0x705530, 'app: start'), (0x705627, 'app: network / Steam / EOS'), (0x7056B4, 'app: MapMgr 0x1230A0'),
               (0x7056C0, 'app: before System::update'), (0x70571B, 'app: after System::update'),
               (0x7058B0, 'app: HUD 0x11977F0'), (0x7058BD, 'app: input wrapper 0x77320'), (0x7058CD, 'app: rest'))
MAIN_LOOP = (0x11A2E80, 0x11A316E)   # the frame loop around Application::Update (its vtable +8 at 0x11A3006)
MAIN_BUCKETS = ((0x11A2E80, 'main loop: top'), (0x11A2F21, 'main loop: tick wait'),
                (0x11A2FF8, 'main loop: input sample 0x1182A10'), (0x11A3009, 'main loop: after the update'),
                (0x11A305F, 'main loop: render fence wait 0x111CC20'), (0x11A3064, 'main loop: after the fence'),
                (0x11A30A0, 'main loop: Present command 0x111D890'), (0x11A30A1, 'main loop: tail'))
# PERF: `frames, steps` since the frame anchor (src/perf_probe.h); an older build's `N frames` were steps.
PERF = re.compile(r'\[(\d+):(\d+):(\d+(?:\.\d+)?)\] PERF (\d+) frames(?:, (\d+) steps)?: mean ([\d.]+) ms \(\d+ fps\), '
                  r'worst ([\d.]+) ms(?:, (\d+) over 20 ms)?, (\d+) over 33 ms')
FRAME = re.compile(r'\[(\d+):(\d+):(\d+(?:\.\d+)?)\] PERF FRAME (\d+) frames split, (\d+) without a step: a frame input '
                   r'([\d.]+), app ([\d.]+), draw ([\d.]+) \(umbra resolve ([\d.]+)\), steps ([\d.]+) \(([\d.]+) a step\), '
                   r'fence wait ([\d.]+), present command ([\d.]+), tail ([\d.]+) ms \(the tick wait in it\); without the '
                   r'steps and the tail ([\d.]+) ms')
FRAME_PARTS = ('input', 'app', 'draw', 'umbra resolve', 'steps', 'a step', 'fence wait', 'present command', 'tail',
               'without steps')
RENDER = re.compile(r'\[(\d+):(\d+):(\d+(?:\.\d+)?)\] PERF RENDER (\d+) presents: the call ([\d.]+) ms \(worst ([\d.]+)\), '
                    r'([\d.]+) ms apart \(worst ([\d.]+)\); input to present ([\d.]+) ms \(the frame\'s own sample\), '
                    r'([\d.]+) ms \(worst ([\d.]+); [^)]*\), (\d+) unpaired; submits a frame by pass ([\d.]+) ([\d.]+) '
                    r'([\d.]+) ([\d.]+) ([\d.]+), other ([\d.]+); mesh draws a frame ([\d.]+)')
NETCH = re.compile(r'\[(\d+):(\d+):(\d+(?:\.\d+)?)\] PERF NET CHANNELS out: (.*?); in: (.*?); world walks (\d+) '
                   r'\(([\d.]+) ms, ([\d.]+) a frame\)')
NETROW = re.compile(r'((?:bit|head) 0x[0-9A-Fa-f]+) (\d+)/(\d+) B')
SLOW = re.compile(r'SLOW FRAME (\d+) (\d+) ([\d.]+) ms, (\d+) steps(?:: app ([\d.]+), draw ([\d.]+), steps ([\d.]+), '
                  r'fence ([\d.]+), tail ([\d.]+))?')
# EDF6Coop's log stamps the date too ([2026-11-24 13:00:15.000], edf-coop-stable src/log.cpp)
COOP = re.compile(r'\[(?:\d{4}-\d\d-\d\d )?(\d+):(\d+):(\d+(?:\.\d+)?)\].*?STATS last (\d+)s: game sends ([\d.]+) kbps avg, busiest second '
                  r'([\d.]+) kbps, to (\d+) players.*?wire up ([\d.]+) kbps down ([\d.]+) kbps')


def vanilla_dirs() -> tuple[str, str]:
    """The two name sources: this machine's vanilla code index (not in any repository) and the edf6-vanilla-re clone.
    $EDF6_VANILLA_CODE / $EDF6_VANILLA_RE, else where they sit on this machine: vanilla-code in the folder that holds
    the main checkout, edf6-vanilla-re next to that folder."""
    import subprocess
    code, re_ = os.environ.get('EDF6_VANILLA_CODE', ''), os.environ.get('EDF6_VANILLA_RE', '')
    if not code or not re_:
        try:
            common = subprocess.run(['git', '-C', ROOT, 'rev-parse', '--path-format=absolute', '--git-common-dir'],
                                    capture_output=True, text=True, timeout=20).stdout.strip()
        except (OSError, subprocess.SubprocessError):
            common = ''
        if common:
            home = os.path.dirname(os.path.dirname(common))   # common is <home>/main/.git
            code = code or os.path.join(home, 'vanilla-code')
            re_ = re_ or os.path.join(os.path.dirname(home), 'edf6-vanilla-re')
    return code, re_


def vanilla_names(sha: str, code: str, re_: str) -> dict[int, str]:
    """EDF.dll's function names by RVA for the build of sha256 `sha`: edf6-vanilla-re's shared/functions (the reviewed
    names) over the vanilla code index's (its functions.tsv but the bare Ghidra address labels). A source missing (CI
    has neither), or made from another build, gives none: the frames are then shown by RVA alone."""
    names: dict[int, str] = {}
    try:
        with open(os.path.join(code, 'function-index', 'summary.json'), encoding='utf-8') as f:
            same = json.load(f).get('images_sha256', {}).get('EDF.dll', '').lower() == sha
        if same:
            with open(os.path.join(code, 'function-index', 'functions.tsv'), encoding='utf-8', newline='') as f:
                for r in csv.DictReader(f, delimiter='\t'):
                    if r['module'] == 'EDF.dll' and r['label_kind'] != 'address_label':
                        names[int(r['rva'], 16)] = r['label']
    except (OSError, ValueError, KeyError):
        pass
    try:
        with open(os.path.join(re_, 'shared', 'builds.tsv'), encoding='utf-8', newline='') as f:
            builds = {r['build'] for r in csv.DictReader(f, delimiter='\t')
                      if r['module'] == 'EDF.dll' and r['sha256'].lower() == sha}
        if builds:
            with open(os.path.join(re_, 'shared', 'functions', 'EDF.dll.tsv'), encoding='utf-8', newline='') as f:
                for r in csv.DictReader(f, delimiter='\t'):
                    if r['build'] in builds:
                        names[int(r['rva'], 16)] = r['name']
    except (OSError, ValueError, KeyError):
        pass
    return names


class Image:
    """EDF.dll's .pdata (function ranges), its vtables' slots by class (RTTI) and the functions' names where the vanilla
    indexes know them (vanilla_names), for naming stack frames."""

    def __init__(self, dll: str, classes: bool = True, names: bool = True) -> None:
        import pefile
        pe = pefile.PE(dll, fast_load=True)
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION']])
        self.pdata = sorted((e.struct.BeginAddress, e.struct.EndAddress) for e in pe.DIRECTORY_ENTRY_EXCEPTION)
        self.starts = [b for b, _ in self.pdata]
        self.slot_class: dict[int, str] = {}
        self.names: dict[int, str] = vanilla_names(file_sha256(dll), *vanilla_dirs()) if names else {}
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
        label = self.names.get(rva) or self.slot_class.get(rva)
        return f'{rva:#x}' + (f' {label}' if label else '')


def frame_rva(frame: str) -> int | None:
    mod, _, off = frame.rpartition('+')
    return int(off, 16) if mod.lower() == 'edf.dll' else None


def module(frame: str) -> str:
    return frame.rpartition('+')[0]


def is_plugin(frame: str) -> bool:
    # .dll: EDF6.exe, the game's own launcher at the stack's root, shares the prefix
    m = module(frame).lower()
    return m.startswith(PLUGIN_MODULES) and m.endswith('.dll')


class _Dbghelp:
    """dbghelp in this process, one session for every plugin module (each loaded at its own fake base)."""

    def __init__(self) -> None:
        import ctypes
        import ctypes.wintypes as wt
        self.ctypes = ctypes
        self.dbg = ctypes.WinDLL('dbghelp.dll')
        self.proc = wt.HANDLE(0x45444636)   # any value: no real process, modules are loaded by hand
        # UNDNAME (names as in the source), FAIL_CRITICAL_ERRORS, EXACT_SYMBOLS (a PDB of another build is refused)
        self.dbg.SymSetOptions(0x2 | 0x200 | 0x400)
        self.dbg.SymInitializeW.argtypes = [wt.HANDLE, wt.LPCWSTR, wt.BOOL]
        self.dbg.SymSetSearchPathW.argtypes = [wt.HANDLE, wt.LPCWSTR]
        self.dbg.SymLoadModuleExW.restype = ctypes.c_uint64
        self.dbg.SymLoadModuleExW.argtypes = [wt.HANDLE, wt.HANDLE, wt.LPCWSTR, wt.LPCWSTR, ctypes.c_uint64, wt.DWORD,
                                              ctypes.c_void_p, wt.DWORD]
        self.dbg.SymFromAddr.argtypes = [wt.HANDLE, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_void_p]
        if not self.dbg.SymInitializeW(self.proc, None, False):
            raise OSError('SymInitialize failed')
        self.next_base = 0x10000000

        class SymbolInfo(ctypes.Structure):
            _fields_ = [('SizeOfStruct', wt.ULONG), ('TypeIndex', wt.ULONG), ('Reserved', ctypes.c_uint64 * 2),
                        ('Index', wt.ULONG), ('Size', wt.ULONG), ('ModBase', ctypes.c_uint64), ('Flags', wt.ULONG),
                        ('Value', ctypes.c_uint64), ('Address', ctypes.c_uint64), ('Register', wt.ULONG),
                        ('Scope', wt.ULONG), ('Tag', wt.ULONG), ('NameLen', wt.ULONG), ('MaxNameLen', wt.ULONG),
                        ('Name', ctypes.c_char * 1024)]
        self.SymbolInfo = SymbolInfo

    def load(self, dll: str) -> int | None:
        self.dbg.SymSetSearchPathW(self.proc, os.path.dirname(os.path.abspath(dll)))
        base = self.dbg.SymLoadModuleExW(self.proc, None, dll, None, self.next_base, 0x8000000, None, 0)
        if not base:
            return None
        self.next_base += 0x10000000
        return base

    def symbol(self, base: int, rva: int) -> tuple[int, str] | None:
        """(function start RVA, name) from the PDB; None without one (no symbol, or only the nearest export)."""
        s = self.SymbolInfo()
        s.SizeOfStruct = 88   # the fixed part, without the name
        s.MaxNameLen = 1023
        disp = self.ctypes.c_uint64()
        if not self.dbg.SymFromAddr(self.proc, base + rva, self.ctypes.byref(disp), self.ctypes.byref(s)):
            return None
        if s.Flags & 0x200:   # SYMFLAG_EXPORT: the nearest export, not the function
            return None
        return s.Address - base, s.Name.decode(errors='replace')


def file_sha256(path: str) -> str:
    import hashlib
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def short_name(name: str) -> str:
    """A PDB name made readable: anonymous namespaces dropped, and a long template's arguments (lambdas) elided."""
    name = re.sub(r"`anonymous namespace'::|A0x[0-9a-f]+::", '', name)
    if len(name) > 80:
        # an operator's own < > (operator<, operator->, operator>>=) are not template brackets: mask them,
        # same length, so the indices still fit the name
        masked = re.sub(r'operator\s*(?:<=>|<<=|>>=|->\*|<<|>>|<=|>=|->|<|>)',
                        lambda m: '_' * len(m.group(0)), name)
        i, j = masked.find('<'), masked.rfind('>')
        if 0 <= i < j:
            name = name[:i] + '<...>' + name[j + 1:]
    return name


INSTALLED_DLL = 'EDF6VehicleCrew.dll'   # perfrun's install name for the run's DLL (tools/perfrun.py)


class Plugins:
    """Names plugin frames by function. Only the run's own DLL (perfrun placed it; its sha256 is in run.json) is
    resolved: by the PDB next to it (dbghelp), else by its .pdata. The other plugin modules in the game folder are
    whatever was installed, so a file of the same name elsewhere may be another build (wrong names, as seen with
    EDF6AutoTurret 2026-11-07); their frames keep their own RVA, as does every frame without the DLL at hand."""

    _dbghelp: _Dbghelp | None = None

    def __init__(self, dll: str | None, sha: str | None = None) -> None:
        self.dll = dll if dll and os.path.isfile(dll) and (not sha or sha == file_sha256(dll)) else None
        self.mods: dict[str, tuple[int | None, Image | None]] = {}
        self.cache: dict[tuple[str, int], tuple[str, str]] = {}

    def _module(self, mod: str) -> tuple[int | None, Image | None]:
        if mod not in self.mods:
            base, img = None, None
            # perfrun installs whatever --dll names as Plugins/EDF6VehicleCrew.dll, so the stacks carry that name,
            # not the source file's
            dll = self.dll if mod.lower() == INSTALLED_DLL.lower() else None
            if dll:
                try:
                    img = Image(dll, classes=False, names=False)
                except Exception:   # pefile missing or not a PE: RVAs as they are
                    img = None
                if os.path.isfile(os.path.splitext(dll)[0] + '.pdb'):
                    try:
                        if Plugins._dbghelp is None:
                            Plugins._dbghelp = _Dbghelp()
                        base = Plugins._dbghelp.load(dll)
                    except (OSError, AttributeError):   # not Windows, or dbghelp refused
                        base = None
            self.mods[mod] = (base, img)
        return self.mods[mod]

    def func(self, frame: str, leaf: bool) -> tuple[str, str]:
        """(key, name) of the function a plugin frame is in. Return addresses (every frame but the leaf) are looked up
        one byte back: a call that ends its function returns to the next one."""
        mod, _, off = frame.rpartition('+')
        rva = int(off, 16) - (0 if leaf else 1)
        k = (mod, rva)
        if k not in self.cache:
            base, img = self._module(mod)
            sym = Plugins._dbghelp.symbol(base, rva) if base and Plugins._dbghelp else None
            if sym:
                self.cache[k] = (f'{mod}+{sym[0]:x}', short_name(sym[1]))
            else:
                start = img.start(rva) if img else None
                key = f'{mod}+{start if start is not None else int(off, 16):x}'
                self.cache[k] = (key, key)
        return self.cache[k]


def read_timed_samples(path: str) -> dict[str, list[tuple[int | None, list[str]]]]:
    """rip_sampler's T lines by thread: (QPC when it was taken, None from an older sampler; the stack, leaf first)."""
    out: dict[str, list[tuple[int | None, list[str]]]] = collections.defaultdict(list)
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            p = line.split()
            if len(p) >= 4 and p[0] == 'T':
                if p[3].startswith('@'):
                    if len(p) >= 5:
                        out[p[1]].append((int(p[3][1:]), p[4:]))
                else:
                    out[p[1]].append((None, p[3:]))
    return out


def read_samples(path: str) -> dict[str, list[list[str]]]:
    """rip_sampler's T lines by thread: each a stack, leaf first."""
    return {t: [st for _, st in v] for t, v in read_timed_samples(path).items()}


def _tod(t: float) -> float:
    lt = time.localtime(t)
    return lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec + (t % 1)


def _rows(log: str, pattern: re.Pattern, window: tuple[float, float] | None) -> list[re.Match]:
    """The log's rows matching `pattern` (its first three groups the time of day) inside `window` (wall-clock seconds;
    a row covers the 5 s before it, so one must end 5 s in), all of them without one."""
    try:
        with open(log, encoding='utf-8', errors='replace') as f:
            lines = f.read().splitlines()
    except OSError:
        return []
    a, b = (_tod(window[0]), _tod(window[1])) if window else (None, None)
    out = []
    for line in lines:
        m = pattern.search(line)
        if not m:
            continue
        t = int(m[1]) * 3600 + int(m[2]) * 60 + float(m[3])
        if a is None or a + 5 <= t <= b + 0.5:
            out.append(m)
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


def read_perf(log: str, window: tuple[float, float]) -> list[tuple]:
    """The plugin's PERF rows (each the 5 s before its time) that lie inside `window` (wall-clock seconds): (time of
    day, frames, mean ms, worst ms, over 20 ms or None on an older build, over 33 ms, steps or None on a build that
    counted steps as frames)."""
    return [(int(m[1]) * 3600 + int(m[2]) * 60 + float(m[3]), int(m[4]), float(m[6]), float(m[7]),
             int(m[8]) if m[8] else None, int(m[9]), int(m[5]) if m[5] else None) for m in _rows(log, PERF, window)]


def read_frame_split(log: str, window: tuple[float, float]) -> dict[str, float]:
    """The PERF FRAME rows (PerfProbe=1) in `window`: each part's ms a frame, weighted by the frames split; 'frames'
    and 'no step' their counts. {} without such rows."""
    rows = _rows(log, FRAME, window)
    n = sum(int(m[4]) for m in rows)
    if not n:
        return {}
    out = {name: sum(int(m[4]) * float(m[6 + i]) for m in rows) / n for i, name in enumerate(FRAME_PARTS)}
    steps = sum(int(m[4]) * float(m[10]) for m in rows)
    stepped = sum(int(m[4]) * float(m[10]) / float(m[11]) for m in rows if float(m[11]) > 0)
    out['a step'] = steps / stepped if stepped else 0.0   # the steps' time over their count, not a mean of means
    out['frames'] = n
    out['no step'] = sum(int(m[5]) for m in rows)
    return out


def read_render(log: str, window: tuple[float, float]) -> dict[str, float]:
    """The PERF RENDER rows in `window`: means weighted by presents, worsts the max. {} without them."""
    rows = _rows(log, RENDER, window)
    n = sum(int(m[4]) for m in rows)
    if not n:
        return {}

    def mean(i: int) -> float:
        return sum(int(m[4]) * float(m[i]) for m in rows) / n
    return {'presents': n, 'call_ms': mean(5), 'call_worst': max(float(m[6]) for m in rows), 'apart_ms': mean(7),
            'apart_worst': max(float(m[8]) for m in rows), 'own_ms': mean(9), 'stepped_ms': mean(10),
            'stepped_worst': max(float(m[11]) for m in rows), 'unpaired': sum(int(m[12]) for m in rows),
            'submits': [mean(i) for i in range(13, 19)], 'meshes': mean(19)}


def read_net(log: str, window: tuple[float, float], secs: float) -> dict:
    """The PERF NET CHANNELS rows in `window`: each channel's messages and bytes a second, out and in, and the NetId
    resolver's world walks (a frame, ms in all). {} without them."""
    rows = _rows(log, NETCH, window)
    if not rows:
        return {}
    chans: dict[tuple[str, str], list[int]] = collections.defaultdict(lambda: [0, 0])
    for m in rows:
        for way, text in (('out', m[4]), ('in', m[5])):
            for r in NETROW.finditer(text):
                chans[(way, r[1])][0] += int(r[2])
                chans[(way, r[1])][1] += int(r[3])
    return {'channels': {k: (v[0] / secs, v[1] / secs) for k, v in chans.items()},
            'walks': sum(int(m[6]) for m in rows), 'walk_ms': sum(float(m[7]) for m in rows),
            'walks_a_frame': statistics.mean(float(m[8]) for m in rows)}


def read_coop(log: str, window: tuple[float, float]) -> list[tuple[float, float, int, float, float]]:
    """EDF6Coop's STATS rows in `window`: (game kbps, busiest second kbps, players, wire up kbps, wire down kbps)."""
    return [(float(m[5]), float(m[6]), int(m[7]), float(m[8]), float(m[9])) for m in _rows(log, COOP, window)]


def read_slow(log: str) -> list[tuple[int, int, float, int, tuple | None]]:
    """The SLOW FRAME rows: (QPC begin, QPC end, ms, steps, (app, draw, steps, fence, tail) ms or None)."""
    out = []
    try:
        with open(log, encoding='utf-8', errors='replace') as f:
            for line in f:
                m = SLOW.search(line)
                if m:
                    out.append((int(m[1]), int(m[2]), float(m[3]), int(m[4]),
                                tuple(float(m[i]) for i in range(5, 10)) if m[5] else None))
    except OSError:
        pass
    return out


def in_slow(samples: list[tuple[int | None, list[str]]], slow: list[tuple]) -> list[list[str]]:
    """The stacks taken inside a slow frame's [begin, end] (QPC)."""
    spans = sorted((s[0], s[1]) for s in slow)
    begins = [b for b, _ in spans]
    out = []
    for q, st in samples:
        if q is None:
            continue
        i = bisect.bisect_right(begins, q) - 1
        if i >= 0 and q <= spans[i][1]:
            out.append(st)
    return out


LOAD_PHASE = re.compile(r'\[(\d+):(\d+):(\d+(?:\.\d+)?)\] MISSION phase (.+?) \((.+?) (\d+) ms\)$')
LOAD_DONE = re.compile(r'\[(\d+):(\d+):(\d+(?:\.\d+)?)\] MISSION phases done \((.+?) (\d+) ms\)')
LOAD_MEM = re.compile(r'\[(\d+):(\d+):(\d+(?:\.\d+)?)\] MEMORY (.+?): the game (\d+) MB committed \(peak (\d+)\), (\d+) MB in RAM')


def read_loads(log: str) -> list[dict]:
    """The mission entries in a run's log (hitch-5), in order: from the game's player preload (MISSION phase "the
    game's player preload") through the plugin's phases (each with its ms) to MISSION phases done and MEMORY the map in.
    Each: 'phases' [(name, ms)], 'preload_s' (player preload to phases done), 'map_s' (phases done to the map in),
    'memory' {point: (committed, peak, in RAM) MB}, 'entry' (1 = the launch's first: its caches cold)."""
    try:
        with open(log, encoding='utf-8', errors='replace') as f:
            lines = f.read().splitlines()
    except OSError:
        return []
    out: list[dict] = []
    cur: dict | None = None
    for line in lines:
        m = LOAD_PHASE.search(line)
        t = (int(m[1]) * 3600 + int(m[2]) * 60 + float(m[3])) if m else None
        if m and m[4] == "the game's player preload":
            cur = {'start': t, 'phases': [], 'memory': {}, 'entry': len(out) + 1}
            out.append(cur)
            continue
        if cur is None:
            continue
        if m:
            cur['phases'].append((m[5], int(m[6])))
            continue
        d = LOAD_DONE.search(line)
        if d:
            cur['phases'].append((d[4], int(d[5])))
            cur['done'] = int(d[1]) * 3600 + int(d[2]) * 60 + float(d[3])
            continue
        mm = LOAD_MEM.search(line)
        if mm:
            cur['memory'][mm[4]] = (int(mm[5]), int(mm[6]), int(mm[7]))
            if mm[4] == 'the map in':
                cur['map'] = int(mm[1]) * 3600 + int(mm[2]) * 60 + float(mm[3])
    for e in out:
        def span(a: float | None, b: float | None) -> float | None:
            return None if a is None or b is None else (b - a) % 86400   # past midnight
        e['preload_s'] = span(e['start'], e.get('done'))
        e['map_s'] = span(e.get('done'), e.get('map'))
    return out


def loads_text(loads: list[dict]) -> list[str]:
    if not loads:
        return ['no mission entry in the log (MISSION phase rows)']
    out = ['| entry | cache | player preload to phases done (s) | to the map in (s) | slowest phases (ms) | committed at '
           'the map in (MB) |', '|---|---|---|---|---|---|']
    for e in loads:
        slow = ', '.join(f'{n} {ms}' for n, ms in sorted(e['phases'], key=lambda p: -p[1])[:4] if ms)
        mem = e['memory'].get('the map in')

        def f(v: float | None) -> str:
            return 'n/a' if v is None else f'{v:.2f}'
        out.append(f'| {e["entry"]} | {"cold (the launch first)" if e["entry"] == 1 else "warm (again)"} | '
                   f'{f(e["preload_s"])} | {f(e["map_s"])} | {slow or "-"} | {mem[0] if mem else "n/a"} |')
    return out


def presentmon_summary(path: str) -> dict:
    """PresentMon's CSV (perfrun --presentmon; v1 columns, or v2 with --v1_metrics): the display side (latency-2):
    ms between display changes (mean, 95th, 99th percentile), presents not displayed, ms until displayed, present modes."""
    try:
        with open(path, newline='', encoding='utf-8', errors='replace') as f:
            rows = list(csv.DictReader(f))
    except OSError:
        return {}
    if not rows:
        return {}

    def col(*names: str) -> list[float]:
        for n in names:
            if n in rows[0]:
                vals = []
                for r in rows:
                    try:
                        vals.append(float(r[n]))
                    except (TypeError, ValueError):
                        pass
                return vals
        return []

    def pct(v: list[float], q: float) -> float:
        v = sorted(v)
        return v[min(len(v) - 1, int(len(v) * q))] if v else 0.0
    change = [v for v in col('MsBetweenDisplayChange', 'DisplayedTime') if v > 0]
    until = [v for v in col('MsUntilDisplayed', 'DisplayLatency') if v > 0]
    dropped = sum(1 for r in rows if str(r.get('Dropped', r.get('FrameType', ''))).strip() in ('1', 'NotDisplayed'))
    return {'presents': len(rows), 'change_mean': statistics.mean(change) if change else 0.0,
            'change_p95': pct(change, 0.95), 'change_p99': pct(change, 0.99), 'dropped': dropped,
            'until_mean': statistics.mean(until) if until else 0.0,
            'modes': dict(collections.Counter(r.get('PresentMode', '?') for r in rows))}


OBJECTS = re.compile(r'\[(\d+):(\d+):(\d+(?:\.\d+)?)\] PERF OBJECTS enemies (-?\d+), player team (-?\d+), friends (-?\d+)')


def read_objects(log: str, window: tuple[float, float]) -> dict:
    """The plugin's PERF OBJECTS rows (what the game held: its own team counts, the living only) in `window`: each
    team's mean and max, {} on a build without them."""
    def tod(t: float) -> float:
        lt = time.localtime(t)
        return lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec + (t % 1)
    a, b = tod(window[0]), tod(window[1])
    rows = []
    try:
        with open(log, encoding='utf-8', errors='replace') as f:
            for line in f:
                m = OBJECTS.search(line)
                if m and a + 5 <= int(m[1]) * 3600 + int(m[2]) * 60 + float(m[3]) <= b + 0.5:
                    rows.append((int(m[4]), int(m[5]), int(m[6])))
    except OSError:
        return {}
    if not rows:
        return {}
    return {name: (sum(r[i] for r in rows) / len(rows), max(r[i] for r in rows))
            for i, name in enumerate(('enemies', 'player team', 'friends'))}


def objects_text(objs: dict) -> str:
    return ', '.join(f'{k} {m:.0f} (max {x})' for k, (m, x) in objs.items())


def frame_summary(rows) -> dict:
    """The PERF rows summed. 'steps_per_frame' None on an older build, whose rows counted steps ('unit' says which)."""
    rows = [r for r in rows if r[1]]
    if not rows:
        return {}
    frames = sum(r[1] for r in rows)
    mean = sum(r[1] * r[2] for r in rows) / frames
    over20 = None if any(r[4] is None for r in rows) else sum(r[4] for r in rows)
    steps = None if any(len(r) < 7 or r[6] is None for r in rows) else sum(r[6] for r in rows)
    return {'rows': len(rows), 'frames': frames, 'mean_ms': mean, 'fps': 1000 / mean,
            'over20_pct': None if over20 is None else over20 / frames * 100, 'over33': sum(r[5] for r in rows),
            'worst_median_ms': statistics.median(r[3] for r in rows), 'worst_max_ms': max(r[3] for r in rows),
            'steps': steps, 'steps_per_frame': None if steps is None else steps / frames,
            'unit': 'frame' if steps is not None else 'step (an older build: its PERF counted steps as frames)'}


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
    # the main loop (at full speed mostly in its tick wait and the render fence, rarely in System::update); before the
    # roles: every xgs thread, this one too, starts in 0x3E630
    loop = sum(1 for st in stacks if any(r is not None and MAIN_LOOP[0] <= r < MAIN_LOOP[1] for r in map(frame_rva, st)))
    if loop > len(stacks) * 0.2:
        return 'game thread (main loop)'
    for fn, role in ROLES:
        if starts[fn] > len(stacks) * 0.2:
            return role
    leaf = collections.Counter(module(st[0]) for st in stacks).most_common(2)
    return 'leaf ' + ', '.join(f'{m} {c * 100 // len(stacks)}%' for m, c in leaf)


def bucket(table: tuple, ret: int) -> str:
    """The label of the last (lowest address, label) entry at or below `ret`."""
    label = table[0][1]
    for a, n in table:
        if ret >= a:
            label = n
    return label


def phase_of(img: Image, st: list[str]) -> tuple[str, int, list[int | None], list[int | None]]:
    """(phase, the index of the frame that names it, the stack's RVAs, their functions' starts) of a game thread stack:
    a System::update phase (the draw split into its parts, a phase's wait on the xgs pool apart), else the
    Application::Update call or the main loop's part it is in."""
    rvas = [frame_rva(f) for f in st]
    starts = [img.start(r) if r is not None else None for r in rvas]
    if SYSTEM_UPDATE in starts:
        i = starts.index(SYSTEM_UPDATE)
        key = 'setup'
        for a, n in PHASES:
            if rvas[i] > a:
                key = n
        below = set(starts[:i])
        if key.startswith('setup: draw'):
            key = 'draw: ' + next((n for fn, n in DRAW_PARTS if fn in below), 'the rest')
        elif POOL_WAIT in below:
            key += ' (waiting on the xgs pool 0x65F10)'
        return key, i, rvas, starts
    if APP_FRAME in starts:
        i = starts.index(APP_FRAME)
        return bucket(APP_BUCKETS, rvas[i]), i, rvas, starts
    for i, r in enumerate(rvas):
        if r is not None and MAIN_LOOP[0] <= r < MAIN_LOOP[1]:
            return bucket(MAIN_BUCKETS, r), i, rvas, starts
    return 'outside the frame loop', len(st), rvas, starts


def phase_ms(key: str, share: float, frame_ms: float | None, spf: float | None) -> str:
    """` x ms/frame` (and ` y ms/step` for a step phase) of a phase with `share` of the game thread's samples."""
    if not frame_ms:
        return ''
    out = f' {share * frame_ms:6.2f} ms/frame'
    if spf and key.split(' (')[0] in STEP_PHASES:
        out += f' {share * frame_ms / spf:6.2f} ms/step'
    return out


def phases(img: Image, stacks: list[list[str]], top: int, plug: Plugins | None = None,
           frame_ms: float | None = None, spf: float | None = None) -> list[str]:
    """The game thread by System::update phase, and below each the class whose virtual the phase loop called, or the
    plugin function it called (a hook on a vtable slot, e.g. the soldiers' Think on slot 7 in ai_update). With the
    mean frame and the steps a frame: each phase's ms a frame, and a step phase's ms a step."""
    plug = plug or Plugins(None)
    ph = collections.Counter()
    below: dict[str, collections.Counter] = collections.defaultdict(collections.Counter)
    for st in stacks:
        key, i, rvas, starts = phase_of(img, st)
        ph[key] += 1
        # the outermost plugin frame when the phase reached the plugin before any EDF.dll function: the plugin's
        # time, not the phase's own (the measurement probes, perf_probe.cpp, only time and call through)
        hook = None
        for j in range(i - 1, -1, -1):
            if rvas[j] is not None:
                break
            if is_plugin(st[j]):
                name = plug.func(st[j], j == 0)[1]
                if 'perfprobe' in name:
                    continue
                hook = '[plugin] ' + name
                break
        chain = [img.name(s) for s in reversed(starts[:i]) if s is not None][:2]
        if hook:
            label = hook
        elif chain:
            label = ' > '.join(chain) + (' [plugin below]' if any(is_plugin(f) for f in st[:i]) else '')
        else:
            label = '(in the phase itself / system code)'
        below[key][label] += 1
    n = len(stacks)
    out = []
    for k, v in ph.most_common():
        out.append(f'- {v / n * 100:5.1f}%{phase_ms(k, v / n, frame_ms, spf)} {k}')
        for c, m in below[k].most_common(top // 2):
            out.append(f'  - {m / n * 100:5.1f}%{phase_ms(k, m / n, frame_ms, spf)} {c}')
    return out


def plugin_owns(plug: 'Plugins', st: list[str]) -> bool:
    """Whether a sample is the plugin's own work: its deepest plugin frame (the measurement probes not counted) is not a
    hook that only calls through to the game (a *Hook whose callee, the next frame leafward, is EDF.dll: the stock Think
    under ThinkHook, the stock render under RenderHook). What the plugin's own code calls in the game counts: the
    deepest plugin frame is then that code, not the hook."""
    for j, f in enumerate(st):
        if not is_plugin(f):
            continue
        name = plug.func(f, j == 0)[1]
        if 'perfprobe' in name:
            continue
        return not ('Hook' in name and j > 0 and frame_rva(st[j - 1]) is not None)
    return False


def plugin_functions(plug: Plugins, stacks: list[list[str]], top: int, frame_ms: float | None) -> list[str]:
    """The plugin's functions on a thread: inclusive (on the stack at all) and self (the deepest frame outside the
    system DLLs, so the system calls below it count but EDF.dll work it called does not), as a share of the thread's
    samples and ms of each frame (share x mean frame time; meaningful for the game thread)."""
    n = len(stacks)
    incl: collections.Counter = collections.Counter()
    selfc: collections.Counter = collections.Counter()
    names: dict[str, str] = {}
    for st in stacks:
        seen = set()
        own: str | bool | None = None
        for j, f in enumerate(st):
            if not is_plugin(f):
                if own is None and frame_rva(f) is not None:
                    own = False   # EDF.dll is deepest: the sample is the game's
                continue
            key, name = plug.func(f, j == 0)
            if 'perfprobe' in name:
                continue   # the measurement probes only time and call through: what is below them is not theirs
            names[key] = name
            seen.add(key)
            if own is None:
                own = key
        if own:
            selfc[own] += 1
        for k in seen:
            incl[k] += 1
    if not incl:
        return ['no plugin frames']
    anyp = sum(1 for st in stacks if any(is_plugin(f) and 'perfprobe' not in plug.func(f, j == 0)[1]
                                         for j, f in enumerate(st)))
    ownp = sum(1 for st in stacks if plugin_owns(plug, st))

    def row(k: str, c: int) -> str:
        ms = f' {c / n * frame_ms:6.2f} ms/frame' if frame_ms else ''
        label = names[k] if names[k] == k else f'{names[k]} ({k})'
        return f'- {c / n * 100:5.1f}%{ms} {label}'
    ms = f', {anyp / n * frame_ms:.2f} ms/frame' if frame_ms else ''
    own_ms = f', {ownp / n * frame_ms:.2f} ms/frame' if frame_ms else ''
    out = [f'the plugin on the stack (the measurement probes not counted): {anyp / n * 100:.1f}%{ms}',
           f"the plugin's own work (not the game's own under a hook that only calls through): {ownp / n * 100:.1f}%{own_ms}",
           '', 'inclusive:', '']
    out += [row(k, c) + f' (self {selfc[k] / n * 100:.1f}%)' for k, c in incl.most_common(top)]
    out += ['', 'self (system calls below included):', '']
    out += [row(k, c) for k, c in selfc.most_common(top)]
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
    out = [f'plugin frames on the stack (the measurement probes included): {plugin / n * 100:.1f}%', '', 'self (deepest EDF.dll function, system calls '
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
    log = os.path.join(run_dir, 'vehiclecrew.log')
    fs = frame_summary(read_perf(log, cw))
    out.append('## Frames (clean window)')
    out.append('')
    if fs:
        out.append(f'{fs["frames"]} {fs["unit"]}s: mean {fs["mean_ms"]:.2f} ms ({fs["fps"]:.1f} a second), over 20 ms '
                   + (f'{fs["over20_pct"]:.2f}%' if fs['over20_pct'] is not None else 'n/a (older build)')
                   + f', over 33 ms {fs["over33"]}, worst of each 5 s: median {fs["worst_median_ms"]:.1f} ms, '
                   f'max {fs["worst_max_ms"]:.1f} ms')
        if fs['steps_per_frame'] is not None:
            out.append(f'{fs["steps"]} steps: {fs["steps_per_frame"]:.2f} a frame, {fs["steps"] / (fs["frames"] * fs["mean_ms"] / 1000):.1f} a second '
                       '(59.94 a second is the game\'s full speed; a frame steps at most 3 times)')
    else:
        out.append('no PERF rows in the window (Debug=1 not on, or the window too short)')
    objs = read_objects(log, cw)
    if objs:
        out += ['', 'Living objects (the game\'s team counts, mean over the window): ' + objects_text(objs)]
    split = read_frame_split(log, cw)
    if split:
        n = split['frames']
        out += ['', f'## Frame split (PerfProbe=1; {n} frames, {split["no step"]} without a step)', '',
                '| part | ms a frame |', '|---|---|']
        out += [f'| {k} | {split[k]:.2f} |' for k in FRAME_PARTS if k not in ('a step', 'without steps')]
        out += ['', f'A step: {split["a step"]:.2f} ms. A frame without its steps and its tail (tick wait included '
                f'there): {split["without steps"]:.2f} ms: the floor of a frame that steps nothing, n = 0 (cpu-11), '
                f'at most {1000 / split["without steps"]:.0f} frames a second on this thread alone.'
                if split['without steps'] else '']
    render = read_render(log, cw)
    if render:
        sub = ' / '.join(f'{v:.0f}' for v in render['submits'])
        out += ['', f'## Render thread (PerfProbe=1; {render["presents"]} presents)', '',
                f'- Present call {render["call_ms"]:.2f} ms (worst {render["call_worst"]:.2f}), '
                f'{render["apart_ms"]:.2f} ms apart (worst {render["apart_worst"]:.2f})',
                f'- input sample to Present: {render["own_ms"]:.1f} ms from the frame\'s own sample, '
                f'{render["stepped_ms"]:.1f} ms (worst {render["stepped_worst"]:.1f}) from the sample its scene was '
                f'stepped with, two frames back; {render["unpaired"]} presents unpaired',
                f'- draw submits a frame by pass 0 / 1 / 2 / 3 / 4 / other: {sub}; mesh draws a frame '
                f'{render["meshes"]:.0f}']
    pm = presentmon_summary(os.path.join(run_dir, 'presentmon.csv'))
    if pm:
        out += ['', f'## Display (PresentMon, {pm["presents"]} presents)', '',
                f'- between display changes {pm["change_mean"]:.2f} ms (95th {pm["change_p95"]:.2f}, 99th '
                f'{pm["change_p99"]:.2f}); not displayed {pm["dropped"]}; until displayed {pm["until_mean"]:.2f} ms',
                '- present modes: ' + ', '.join(f'{k} {v}' for k, v in pm['modes'].items())]
    net = read_net(log, cw, secs)
    coop = read_coop(os.path.join(run_dir, 'coop.log'), cw)
    if net or coop:
        out += ['', '## Online (the clean window)', '']
        if net:
            for (way, key), (m, b) in sorted(net['channels'].items(), key=lambda kv: -kv[1][1]):
                out.append(f'- {way} {key}: {m:.1f} messages, {b / 1024:.2f} KB a second')
            out.append(f'- NetId world walks: {net["walks"]} ({net["walk_ms"]:.1f} ms in all, '
                       f'{net["walks_a_frame"]:.2f} a frame)')
        if coop:
            out.append(f'- EDF6Coop: game sends {statistics.mean(r[0] for r in coop):.0f} kbps (busiest second '
                       f'{max(r[1] for r in coop):.0f}), wire up {statistics.mean(r[3] for r in coop):.0f} / down '
                       f'{statistics.mean(r[4] for r in coop):.0f} kbps, {max(r[2] for r in coop)} players')
    loads = read_loads(log)
    if loads:
        out += ['', '## Mission load (hitch-5)', ''] + loads_text(loads)
    cpu = read_cpu(os.path.join(run_dir, 'cpu.txt')) if os.path.exists(os.path.join(run_dir, 'cpu.txt')) else {}
    stacks_path = os.path.join(run_dir, 'stacks.txt')
    timed = read_timed_samples(stacks_path) if os.path.exists(stacks_path) else {}
    samples = {t: [st for _, st in v] for t, v in timed.items()}
    if samples and img is None:
        img = Image(run.get('edf_dll') or edf_dll())
    # the run's own build, if the file is still the one measured: its PDB names the plugin's frames
    plug = Plugins(run.get('dll'), run.get('sha256'))
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
                spf = fs.get('steps_per_frame') if fs else None
                out += ['by phase (ms/frame: the share times the mean frame; ms/step: a step phase\'s over the steps a '
                        'frame):', ''] + phases(img, s, top, plug, fs['mean_ms'] if fs else None, spf) + ['']
                out += ['#### Plugin functions (game thread)', ''] + plugin_functions(
                    plug, s, top, fs['mean_ms'] if fs else None) + ['']
                slow = read_slow(log)
                inside = in_slow(timed[t], slow)
                if inside:
                    out += [f'#### Inside the slow frames ({len(slow)} SLOW FRAME rows in the run, {len(inside)} of '
                            'the thread\'s samples in one)', '', 'by phase:', ''] + phases(img, inside, top, plug)
                    out += [''] + thread_detail(img, inside, top) + ['']
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
        turret = run.get('turret_sha256')
        turret = f'{turret[:8]} ({run.get("turret_source", "?")})' if turret else run.get('turret_source', '?')
        if 'cpu' not in run.get('windows', {}):
            rows.append((run.get('variant', '?'), os.path.basename(d), {}, None, {}, turret))
            continue
        cw = tuple(run['windows']['cpu'])
        fs = frame_summary(read_perf(os.path.join(d, 'vehiclecrew.log'), cw))
        cpu = read_cpu(os.path.join(d, 'cpu.txt')) if os.path.exists(os.path.join(d, 'cpu.txt')) else {}
        secs = cw[1] - cw[0]
        objs = read_objects(os.path.join(d, 'vehiclecrew.log'), cw)
        rows.append((run.get('variant', '?'), os.path.basename(d), fs, cpu.get('total', 0) / 10 / secs if cpu else None,
                     objs, turret))
    out = ['| variant | run | mean ms | steps a frame | over 20 ms | over 33 ms | worst of 5 s (median) | process CPU '
           '(% of a core) | living objects | AutoTurret |', '|---|---|---|---|---|---|---|---|---|---|']
    for v, name, fs, c, objs, turret in sorted(rows, key=lambda r: (r[0], r[1])):
        if not fs:
            out.append(f'| {v} | {name} | ' + ('no PERF rows' if c is not None else 'not sampled') +
                       f' | | | | | {"" if c is None else f"{c:.0f}"} | | {turret} |')
            continue
        o20 = f'{fs["over20_pct"]:.2f}%' if fs['over20_pct'] is not None else 'n/a'
        spf = f'{fs["steps_per_frame"]:.2f}' if fs['steps_per_frame'] is not None else 'n/a (counted steps)'
        out.append(f'| {v} | {name} | {fs["mean_ms"]:.2f} | {spf} | {o20} | {fs["over33"]} | {fs["worst_median_ms"]:.1f} | '
                   f'{c or 0:.0f} | {objects_text(objs)} | {turret} |')
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
