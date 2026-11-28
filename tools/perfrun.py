"""Measure the game in one go: a scenario in the background, sampled once it is busy, a report, everything put back.

  python tools/perfrun.py SCENARIO [options]
  python tools/perfrun.py LEG LEG... [options]     several scenarios in one launch (legs), e.g.
      python tools/perfrun.py stress:1 stress:10 stress:30 stress:100 M001,key=4d@48,key=esc@53 RM015

SCENARIO
  M001 | RM015 | ROW                 a stock offline mission (tests/autopilot/drive.py's)
  grand                              the test range's grand battle (testrange/gen.py grand_battle)
  horde | blast                      heavier swarms (PRESETS): 400 ants; 300 ants under tank shells, rockets, artillery
  stress:N[:ENEMY]                   swarm times N (the user, 2026-11-07: "a hundred times the monsters and NPCs"):
                                     up to 160*N enemies, 3*N soldier squads (past ~1000 soldiers the plugin's
                                     ai::World capacity patch is what keeps the game up, docs/aiworld-re.md),
                                     blast's vehicles shelling them; the PERF OBJECTS rows say how many the game
                                     really held
  swarm[:ENEMY[:PER[:CAP[:LEVEL]]]]  the test range with waves of ENEMY (default giantant01): PER every 2 s while fewer
                                     than CAP live (default 25, 160), at LEVEL (default 0.3, so they live long enough
                                     to pile up), three NPC tanks and three soldier squads fighting them
  bench:E[:S[:ENEMY]]                a pinned population for A/B and scaling (the user, 2026-11-27: stress:N held 2541
                                     enemies one run, 3332 the next): E enemies of ENEMY (default giantant01) on the
                                     range's enemy spots in a fixed order, S soldiers (default BENCH_SOLDIERS = 120,
                                     rounded up to squads of six) by the player, no vehicles. The enemies are mortal
                                     but gen.BENCH_ENEMY_LEVEL (1000) x the stock HP, so none dies in the window
                                     (invincible ones made the plugin's WastedFire skip every target); the soldiers
                                     take no damage from them and come back, the player is invincible, and a refill
                                     loop tops the enemies up to E (a dead one too), so the count stays at E and next
                                     to nothing is spawned in the window. Its warm-up waits for the PERF OBJECTS
                                     enemies to reach E (or stop rising for BENCH_PLATEAU s: the report then says it
                                     fell short), then BENCH_SETTLE s for the fight to close in. Several sizes in one
                                     launch: perfrun.py bench:500 bench:1000 bench:2000 bench:4000; summary.md's
                                     scaling table then gives each phase's ms a step against the enemies and its slope
options
  --dll PATH        EDF6VehicleCrew.dll placed for the run, with the EDF6AutoTurret.dll next to it when there is one
                    (default: this checkout's build; `installed` keeps the game's own)
  --ab A B          two DLLs alternating (--rounds, default 2, so ABAB): frames and CPU only, an A/B table; both
                    with their build's EDF6AutoTurret.dll next to them or neither (refused otherwise: one side would
                    run the game folder's own)
  --warm S          seconds in the mission before sampling (default 45 grand, 30 swarm, 25 a stock mission)
  --cpu S           the clean window (thread CPU, GPU, frame times; nothing suspended), default 20
  --stacks S        the stack window after it, default 20 (0: none)
  --interval MS     stack sampling interval, default 2
  --difficulty N    default 1
  --ini KEY=VALUE   KEY set in EDF6VehicleCrew.ini [VehicleCrew] for the run (repeatable; drive.py --ini), e.g. an A/B
                    of a switch: HavokMultithread=0 against HavokMultithread=1
  --out DIR         default tmp/perf/<scenario>-<time>
  --presentmon EXE  PresentMon (not shipped: it needs ETW, run perfrun as an administrator) over the clean window:
                    presentmon.csv, the display side (latency-2) in the report
  --cycle N         a load run instead (hitch-5): nothing sampled; the mission left and entered again N times in one
                    launch (drive.py --cycle; --cycle-stay S in each, default 20), each entry's phases and memory points
                    from the plugin log, the first one cold (the launch's first), the rest warm: loads.md
  --key SPEC@S[:MS] hold a key S seconds into the mission for MS (default 300; repeatable; drive.py --key): e.g.
                    4d@50 (M, a virtual-key code in hex) opens the map inside the stack window, so its slow frames'
                    stacks are taken (hitch-3 / hitch-6)
  --shots           a picture of the game window every 15 s (PrintWindow: it costs frames, keep it off to measure)
  --restore         put back what a run could not (the game outlived it): the range mission's files, and every file
                    drive.py placed (the DLLs, the ini) byte for byte with the autopilot removed (drive.py restore)

LEGS (more than one positional argument): one launch, the scenarios one after the other in the given order (the
  user, 2026-11-24: a launch for each set of numbers is slow, and Steam says the game is still running). Each leg: its
  mission entered, its own warm-up (as --warm's default for its scenario), the clean window, the stack window, then
  the next leg: the range mission's files rewritten for it when it is a range scenario (gen.rewrite_scenario: the
  originals kept by the first write stay the ones put back), and the autopilot's `next` command (the mission left
  through the pause menu's withdraw, the result screen skipped, back in through HQMain as at the launch: no memory
  written to skip the game's own way; docs/mission-cycle-re.md). A leg is entered when the autopilot logs that its
  mission runs (the loading over: "RUNNING" for the first, "NEXT k entered" for the later ones), its warm-up counted
  from there, and only when the row, the difficulty and the mode's content id it logs are the leg's (a failure
  otherwise); a way in that fails (refused, the pack's mode not entered) ends the run at once, and one not in its
  mission --leg-timeout seconds after it was asked (default 900) ends it there.
  LEG is SCENARIO[,key=SPEC@S[:MS]]...[,warm=S][,diff=N]: keys held S seconds after the leg entered its mission (as
  --key), its own warm-up and difficulty. --key, --ab and --cycle are refused with legs (key= on the leg instead).
  Out: <out>/<leg>/ each (run.json with the leg's windows, cpu.txt, stacks.txt, gpu.csv, its part of the plugin log,
  report.md as a single run's), <out>/summary.md a row a leg; the whole launch's logs in <out>.
  --leg-timeout S   seconds from a leg's `next` until its mission and any enemy wave are ready (default 900: stress:100 leaves at a
                    few frames a second)
  --next-reboot     back in through the script's ReBoot (SystemReset) instead of HQMain
  LEG options for an A/B in one launch (the user, 2026-11-28: 「测试的话想办法快一点，现在每次要测试半天」):
  ini=KEY=VALUE     (repeatable) KEY set in the game's EDF6VehicleCrew.ini for this leg, over the launch's (--ini): the
                    first leg's go into the launch's ini, a later leg's are written before it starts (its `next`, or
                    its stay), and a later leg without ini= has the launch's ini again
  stay              the leg stays in the mission the leg before it is in (no `next`, no loading, no new wave): only its
                    ini written, the plugin reloads it (a hot-reloaded switch: ObjectUpdateMT, NpcParallelThink and
                    the other per-frame ones; a launch-only key changes nothing), STAY_SETTLE s of warm-up (warm=),
                    the clean window, no stack window
  --flip "KEY=VALUE[;KEY=VALUE]"
                    each scenario leg becomes A (as given) then B (these settings), A, B ... as stay legs (2 x --rounds
                    windows, default ABAB) in one launch, summary.md's A/B table of the means a phase; --cpu defaults
                    to FLIP_CPU there. e.g. perfrun.py bench:2000 bench:4000 --flip "ObjectUpdateMT=0;NpcParallelThink=0"
  --normal-priority the game at normal priority (default below normal: tests/autopilot/drive.py BELOW_NORMAL, so the
                    desktop stays usable under a bench of thousands)

What it saves over doing the steps by hand: the range scenarios rewrite only the range mission's four files (gen.
write_scenario) on the installed range, never the full install that regenerates every vehicle and touches Mods; the run
quits as soon as sampling is done instead of staying a fixed time; the report (tools/perf_report.py) is written at the
end. Everything runs under one game lease (tools/gamequeue.py) and the range files, the placed DLLs and the ini are put
back byte for byte: at the run's end, or by --restore when the game outlived the run (drive.py records what it placed
on disk). The game must not be running (it is the user's).
"""
from __future__ import annotations

import hashlib
import json
import math
import os
import re
import shutil
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
for sub in ('pylib', 'testrange', os.path.join('tests', 'autopilot'), 'tools'):
    sys.path.insert(0, os.path.join(ROOT, sub))
import gamedir  # noqa: E402
import gamelease  # noqa: E402

# bench: soldiers by default (the same S at every E, so the slope is the enemies'); seconds after E is reached before
# sampling (the enemies walking in from 180-450 m); how long a count that stops rising is waited on before sampling it
BENCH_SOLDIERS, BENCH_SETTLE, BENCH_PLATEAU = 120, 30.0, 30.0
STAY_SETTLE = 6.0   # a stay leg's warm-up: the ini reloaded (the next frame) and the old setting's frames out of the rows
FLIP_CPU = 12       # --flip's clean window a leg (four of them a scenario), unless --cpu says
BENCH_PINNED = 0.95   # the window's mean enemies under this share of E: the report says the count was not held
WARM = {'grand': 45.0, 'swarm': 30.0, 'horde': 40.0, 'blast': 40.0}   # the waves need this long to fill up; a stock mission's start: 25 s
# (past drive.run's real-mouse nudge at 20 s, which must not fall into the clean window)
SAMPLER = os.path.join(ROOT, 'build', 'rip_sampler.exe')
AUTOPILOT = os.path.join(ROOT, 'build', 'tools', 'EDF6Autopilot.dll')   # tests/autopilot/drive.py installs it
DEFAULT_DLL = os.path.join(ROOT, 'build', 'Mods', 'Plugins', 'EDF6VehicleCrew.dll')


# Heavier range battles (the user, 2026-11-07: "more monsters, and more explosions"): the waves (as swarm's fields)
# and the friends fighting them. horde: 400 ants at once; blast: 300 ants under tank shells, rockets and artillery.
PRESETS = {
    'horde': ('giantant01:40:400:0.3', {'vehicle403_tank_mission': 4}, 4),
    'blast': ('giantant01:40:300:0.3', {'vehicle403_tank_mission': 6, 'edf6tr_katyusha_mission': 2,
                                        'edf6tr_artillery_mission': 2}, 4),
}



def stress_fields(n: int) -> tuple[int, int, float]:
    """stress:N's waves: (per wave, cap, seconds to warm). swarm's cap and squads times N; the waves grow with N so the
    cap can be reached (40 a wave for N up to 5, 400 at most: one wave is one call, a bigger one is a hitch of its own),
    the warm-up as long as filling the cap at that rate takes (2 s a wave), 40 s to 150 s."""
    cap = 160 * n
    per = min(400, 40 * max(1, -(-n // 5)))
    return per, cap, max(40.0, min(150.0, 10.0 + cap / (per / 2.0)))


def plan_for(scenario: str):
    """The range plan of a range scenario, None for a stock mission."""
    import gen
    kind, *rest = scenario.split(':')
    if kind == 'stress':
        n = int(rest[0]) if rest and rest[0] else 1
        if n < 1:
            raise ValueError('stress:N needs N >= 1')
        per, cap, _ = stress_fields(n)
        enemy = rest[1] if len(rest) > 1 and rest[1] else 'giantant01'
        plan = gen.Plan()
        plan.vehicles = {}
        plan.friends = dict(PRESETS['blast'][1])
        plan.squads = 3 * n
        plan.waves = gen.Waves(enabled=True, enemy=enemy, per_wave=per, max_alive=cap, first_delay=3.0, interval=2.0,
                               level=0.3)
        return plan
    if kind == 'bench':
        e = int(rest[0]) if rest and rest[0] else 1000
        soldiers = int(rest[1]) if len(rest) > 1 and rest[1] else BENCH_SOLDIERS
        if e < 1 or soldiers < 0:
            raise ValueError('bench:E[:S[:ENEMY]] needs E >= 1 and S >= 0')
        plan = gen.Plan()
        plan.vehicles = {}
        plan.friends = {}
        plan.scenario = gen.BENCH
        plan.squads = -(-soldiers // 6)
        plan.waves = gen.Waves(enabled=True, enemy=rest[2] if len(rest) > 2 and rest[2] else 'giantant01',
                               per_wave=gen.BENCH_BATCH, max_alive=e, first_delay=3.0, interval=gen.BENCH_GAP,
                               level=gen.BENCH_ENEMY_LEVEL)
        return plan
    if kind == 'grand':
        return gen.grand_battle(gen.Plan())
    if kind in PRESETS:
        # the preset's wave fields, each overridden by one given after it (blast:giantspider01 → spiders)
        rest = [mine if i < len(rest) and rest[i] else theirs for i, (theirs, mine)
                in enumerate(zip(PRESETS[kind][0].split(':'), [*rest, *[''] * 4]))]
    if kind in ('swarm', *PRESETS):
        enemy = rest[0] if len(rest) > 0 and rest[0] else 'giantant01'
        per = int(rest[1]) if len(rest) > 1 else 25
        cap = int(rest[2]) if len(rest) > 2 else 160
        level = float(rest[3]) if len(rest) > 3 else 0.3
        plan = gen.Plan()
        plan.vehicles = {}
        friends, squads = PRESETS[kind][1:] if kind in PRESETS else ({'vehicle403_tank_mission': 3}, 3)
        plan.friends = dict(friends)
        plan.squads = squads
        plan.waves = gen.Waves(enabled=True, enemy=enemy, per_wave=per, max_alive=cap, first_delay=3.0, interval=2.0,
                               level=level)
        return plan
    return None


def sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def warm_for(scenario: str, override: float | None) -> float:
    """Seconds in the mission before sampling: `override` (--warm, a leg's warm=) or the scenario's own (stress:N its
    stress_fields, the range presets WARM, a stock mission 25)."""
    if override is not None:
        return override
    kind, *rest = scenario.split(':')
    if kind == 'stress':
        return stress_fields(int(rest[0]) if rest and rest[0] else 1)[2]
    if kind == 'bench':   # counted from E reached (population_want), not from the mission's start
        return BENCH_SETTLE
    return WARM.get(kind, 25.0)


def is_range(scenario: str) -> bool:
    """Whether `scenario` runs on the test range (plan_for gives it a plan), not a stock mission."""
    return scenario.split(':')[0] in ('stress', 'grand', 'swarm', 'bench', *PRESETS)


def sample(out: str, warm: float, cpu_s: int, stacks_s: int, interval: int, marks: dict,
           presentmon: str | None = None) -> None:
    """The run's `during`: wait `warm`, then the clean window (rip_sampler interval 0 and typeperf side by side), then
    the stack window. A failure is kept in marks['error'] (and ends the stay, as any return does)."""
    try:
        _sample(out, warm, cpu_s, stacks_s, interval, marks, presentmon)
    except Exception as e:   # noqa: BLE001 - kept for the run's verdict; the run must still quit and restore
        marks['error'] = f'{type(e).__name__}: {e}'


def presentmon_args(exe: str, pid: int, secs: int, csv: str) -> list[str]:
    """PresentMon over the clean window: the game's presents only, v1 columns (perf_report.presentmon_summary)."""
    return [exe, '--process_id', str(pid), '--output_file', csv, '--timed', str(secs), '--terminate_after_timed',
            '--v1_metrics', '--no_console_stats']


def _sample(out: str, warm: float, cpu_s: int, stacks_s: int, interval: int, marks: dict,
            presentmon: str | None = None) -> None:
    import drive
    t0 = time.time()
    pids = drive.game_pids()
    if not pids:
        marks['error'] = 'no game process'
        return
    pid = pids[0]
    marks['pid'] = pid
    time.sleep(max(0.0, t0 + warm - time.time()))
    # pid_<pid>_ exactly: pid_<pid>* would take in every process whose id starts with the game's
    gpu = subprocess.Popen(['typeperf', rf'\GPU Engine(pid_{pid}_*)\Utilization Percentage', '-si', '1', '-sc',
                            str(cpu_s), '-f', 'CSV', '-o', os.path.join(out, 'gpu.csv'), '-y'],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    pm = (subprocess.Popen(presentmon_args(presentmon, pid, cpu_s, os.path.join(out, 'presentmon.csv')),
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) if presentmon else None)
    r = subprocess.run([SAMPLER, str(pid), str(cpu_s), '0', os.path.join(out, 'cpu.txt')], capture_output=True, text=True)
    end = time.time()
    if r.returncode:
        marks['error'] = f'rip_sampler (CPU window) exit {r.returncode}: {r.stderr.strip()[-200:]}'
        return
    # rip_sampler times its own `seconds` after attaching (SymInitialize first) and exits right after: the window is
    # the last cpu_s seconds before it returned, not from its start
    marks['windows'] = {'cpu': [end - cpu_s, end]}
    for side in (gpu, pm):
        if side is None:
            continue
        try:
            side.wait(timeout=15)
        except subprocess.TimeoutExpired:
            side.kill()
    if pm is not None and pm.returncode:
        marks['presentmon'] = f'exit {pm.returncode} (an administrator? ETW)'
    if stacks_s > 0:
        r = subprocess.run([SAMPLER, str(pid), str(stacks_s), str(interval), os.path.join(out, 'stacks.txt')],
                           capture_output=True, text=True)
        end = time.time()
        if r.returncode:
            marks['error'] = f'rip_sampler (stacks) exit {r.returncode}: {r.stderr.strip()[-200:]}'
            return
        marks['windows']['stacks'] = [end - stacks_s, end]
        marks['sampler_note'] = r.stderr.strip()[-200:]


def plugin_log(game: str) -> str:
    return os.path.join(game, 'Mods', 'Plugins', 'EDF6VehicleCrew.log')


def coop_log(game: str) -> str:
    """EDF6Coop's log (its STATS rows: what the game sent online), next to the plugin's."""
    return os.path.join(game, 'Mods', 'Plugins', 'EDF6Coop.log')


def turret_alongside(dll: str | None) -> str | None:
    """The EDF6AutoTurret.dll `dll`'s build made next to it, None when there is none (or `--dll installed`)."""
    turret = os.path.join(os.path.dirname(dll), 'EDF6AutoTurret.dll') if dll else None
    return turret if turret and os.path.isfile(turret) else None


def placements(dll: str | None) -> tuple[str, ...]:
    """drive.py --place entries for a run: `dll` as EDF6VehicleCrew.dll and, when its build made one next to it, that
    build's EDF6AutoTurret.dll too (one commit's plugins: the game folder's own AutoTurret may be weeks older and its
    cost was charged to the run). None (`--dll installed`): nothing placed."""
    if not dll:
        return ()
    out = [f'{dll}=Plugins/EDF6VehicleCrew.dll']
    turret = turret_alongside(dll)
    if turret:
        out.append(f'{turret}=Plugins/EDF6AutoTurret.dll')
    return tuple(out)


def keep_symbols(dll: str | None, out: str) -> str | None:
    """A copy of `dll` and its PDB in out/symbols, taken before the launch: perf_report names the plugin's frames from
    the PDB of the very build that ran (an exact match), and the build folder is rebuilt after the run. The copy's path,
    None without a dll or a PDB next to it."""
    if not dll or not os.path.isfile(dll):
        return None
    pdb = os.path.splitext(dll)[0] + '.pdb'
    if not os.path.isfile(pdb):
        return None
    keep = os.path.join(out, 'symbols')
    os.makedirs(keep, exist_ok=True)
    for f in (dll, pdb):
        shutil.copy2(f, os.path.join(keep, os.path.basename(f)))
    return os.path.join(keep, os.path.basename(dll))


def loaded_plugins(game: str, dll: str | None) -> dict:
    """What the game loads in a run with `dll`, taken before the launch (as placements places them): each plugin's
    file, where it comes from and its sha256. AutoTurret: the build's own next to `dll` (built-alongside), else the game
    folder's (game-folder), else none (sha256 None)."""
    plugins = os.path.join(game, 'Mods', 'Plugins')
    crew = dll or os.path.join(plugins, 'EDF6VehicleCrew.dll')
    turret = turret_alongside(dll)
    source = 'built-alongside'
    if not turret:
        turret, source = os.path.join(plugins, 'EDF6AutoTurret.dll'), 'game-folder'
        if not os.path.isfile(turret):
            turret, source = None, 'none'
    return dict(dll_used=crew, sha256=sha256(crew) if os.path.isfile(crew) else None,
                turret=turret, turret_source=source, turret_sha256=sha256(turret) if turret else None)


def ab_turrets(game: str, a: str, b: str) -> str | None:
    """Why an A/B of `a` and `b` would not compare the VehicleCrew builds alone (None: it would): one side with its
    build's AutoTurret, the other with the game folder's."""
    sa, sb = (loaded_plugins(game, d)['turret_source'] for d in (a, b))
    if (sa == 'built-alongside') != (sb == 'built-alongside'):
        return (f'--ab: A runs AutoTurret from {sa}, B from {sb}: put the same build\'s EDF6AutoTurret.dll next to both '
                'DLLs, or next to neither')
    return None


def log_mark(log: str) -> tuple[int, int]:
    """The plugin log's file id and size before a launch ((0, 0): none yet)."""
    try:
        st = os.stat(log)
    except OSError:
        return (0, 0)
    return (st.st_ino, st.st_size)


def run_log(log: str, mark: tuple[int, int]) -> bytes | None:
    """What one launch wrote to the plugin log, `mark` its log_mark from before. Past 32 MB the plugin renames the log to
    .log.1 and opens a new one (src/plugin.cpp): a rename keeps the file id, so the part before the rename is read from
    .log.1 (from `mark`'s size when it is the same file, whole when the log turned over more than once and the start is
    gone) ahead of the new log."""
    if not os.path.exists(log):
        return None
    ino, size = mark
    with open(log, 'rb') as f:
        if ino and os.stat(log).st_ino == ino:
            f.seek(size)
            return f.read()
        now = f.read()
    old = log + '.1'
    if not ino or not os.path.exists(old):
        return now
    with open(old, 'rb') as f:
        if os.stat(old).st_ino == ino:
            f.seek(size)
            return f.read() + now
        return b'(perfrun: the log turned over more than once; its start is lost)\r\n' + f.read() + now


WAVE_OBJECTS = re.compile(rb'(?m)^\[[^\]\r\n]+\] PERF OBJECTS enemies (-?\d+), player team ')


def needs_population(plan, opts: dict) -> bool:
    """Only scenarios which expect waves and have object telemetry can use the population milestone."""
    debug = '1'
    for setting in opts['ini']:
        key, _, value = setting.partition('=')
        if key.strip().lower() == 'debug':
            debug = value.strip().lower()
    return bool(plan and plan.waves.enabled and plan.waves.per_wave > 0 and debug not in ('0', 'false', 'off', 'no'))


def population_want(plan) -> int:
    """The live enemies wait_population waits for: bench's E (its warm-up counts from there), any other wave's first."""
    import gen
    return int(plan.waves.max_alive) if plan is not None and plan.scenario == gen.BENCH else 1


def population_ready(counts: list[tuple[float, int]], want: int, now: float, plateau: float = BENCH_PLATEAU) -> bool:
    """Given the PERF OBJECTS enemies seen so far ((time, count), oldest first): `want` reached, or (want > 1) live
    enemies whose count has not risen for `plateau` s (the game holds no more: sampled anyway, the report says so)."""
    if any(n >= want for _, n in counts):
        return True
    if want <= 1 or not counts or max(n for _, n in counts) <= 0:
        return False
    top = max(n for _, n in counts)
    first_top = next(t for t, n in counts if n == top)
    return now - first_top >= plateau


def wait_population(game: str, log: str, timeout: float, gate=None, want: int = 1) -> str | None:
    """The mission entry can precede actor creation by tens of seconds. Use only object counts appended after it:
    until `want` enemies live (population_ready)."""
    import drive
    mark = log_mark(log)
    deadline = time.monotonic() + timeout
    seen: dict[int, float] = {}   # a row's offset in the log: when it was first read
    while time.monotonic() < deadline:
        if gate is not None and gate.state != drive.Gate.OPEN:
            return 'the run ended before the enemy wave became active'
        if not drive.game_pids():
            return 'the game exited before the enemy wave became active'
        try:
            current = run_log(log, mark) or b''
        except OSError:
            current = b''   # the logger may be rotating the file
        now = time.monotonic()
        counts = [(seen.setdefault(row.start(), now), int(row[1])) for row in WAVE_OBJECTS.finditer(current)]
        if population_ready(counts, want, now):
            return None
        time.sleep(0.5)
    return 'no live enemy wave before the readiness deadline (--leg-timeout)'


RELOADED = re.compile(rb'\] CONFIG reloaded')
RELOAD_TIMEOUT = 15.0   # the plugin looks at the ini's write time once a second, once a frame (a bench's 3 a second)


def wait_reload(log: str, mark: tuple[int, int], timeout: float, gate=None) -> str | None:
    """A stay leg's ini written: until the plugin logs that it reloaded it (src/plugin.cpp ReloadConfigIfChanged)
    after `mark`; why not, when it does not within `timeout` s (the leg's windows would be the leg before's)."""
    import drive
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if gate is not None and gate.state != drive.Gate.OPEN:
            return 'the run ended before the plugin reloaded the ini'
        try:
            if RELOADED.search(run_log(log, mark) or b''):
                return None
        except OSError:
            pass   # the logger may be rotating the file
        time.sleep(0.25)
    return f'the plugin did not reload the ini within {timeout:.0f} s (no CONFIG reloaded row): the leg would be the last one'


def check_wave_window(plan, opts: dict, marks: dict, log: str) -> None:
    """A successful spawn before warm-up is insufficient when the battle is empty during the measurement."""
    window = marks.get('windows', {}).get('cpu')
    if window and not marks.get('error') and needs_population(plan, opts):
        import perf_report
        objects = perf_report.read_objects(log, tuple(window))
        if not objects:
            marks['error'] = 'no object counts in the battle measurement window'
        elif objects['enemies'][1] <= 0:
            marks['error'] = 'no live enemies in the battle measurement window'
        elif population_want(plan) > 1:   # bench: the count it was to hold, and what the window held
            want = population_want(plan)
            marks['population'] = {'want': want, 'mean': objects['enemies'][0], 'max': objects['enemies'][1],
                                   'held': objects['enemies'][0] >= BENCH_PINNED * want}


def one_run(game: str, scenario: str, out: str, dll: str | None, opts: dict, variant: str = '') -> int:
    """One launch of the game: the scenario, sampled, its run.json and logs in `out`; everything put back. Non-zero
    when nothing was sampled, the run failed, or the range's files could not be put back (the game still running)."""
    import drive
    import gen
    if drive.game_pids():
        raise SystemExit('the game is running (the user may be playing): not touched')
    os.makedirs(out, exist_ok=True)
    plan = plan_for(scenario)
    warm = warm_for(scenario, opts['warm'])
    marks: dict = {}
    log = plugin_log(game)
    # the plugin appends to its log across launches: only what this launch writes is this run's
    log_at = log_mark(log)
    clog = coop_log(game)
    clog_at = log_mark(clog)
    cycle = opts.get('cycle')
    wait_for_wave = not cycle and needs_population(plan, opts)
    gate = drive.Gate()

    def during() -> None:
        if wait_for_wave:
            why = wait_population(game, log, opts['leg_timeout'], gate, population_want(plan))
            if why:
                marks['error'] = why
                return
            marks['population_ready'] = time.time()
        sample(out, warm, opts['cpu'], opts['stacks'], opts['interval'], marks, opts.get('presentmon'))

    loaded = loaded_plugins(game, dll)   # before the launch: the files the game is about to load
    kept = keep_symbols(dll, out)
    skipped = gen.write_scenario(game, plan) if plan else []
    rc = 1
    try:
        place = placements(dll)
        stay = opts.get('cycle_stay', 20.0)
        # a load run: each entry its stay and a load (two minutes at most, a big map cold), then CYCLE done
        longest = int(cycle * (stay + 120) + 60) if cycle else int(warm + opts['cpu'] + opts['stacks'] + 90)
        if wait_for_wave:
            longest += math.ceil(opts['leg_timeout'])
        # Debug=1 is the shipped default (EDF6VehicleCrew.ini): the PERF rows, and what players run; PerfProbe=1 the
        # frame split, the render thread and the channels (src/perf_probe.h)
        rc = drive.run(game, 'range' if plan else scenario, opts['difficulty'], longest,
                       os.path.join(out, 'autopilot.log'), place=place, ini=('Debug=1', 'PerfProbe=1', *opts['ini']),
                       shots=os.path.join(out, 'shots') if opts['shots'] else None, keys=tuple(opts.get('keys', ())),
                       during=None if cycle else during, gate=gate,
                       quit_wait=600,   # stress:100 at 8 fps was still leaving the mission after drive's 90 s
                       cycle=(cycle, stay, False) if cycle else None)
    finally:
        # What drive.run left is on disk (drive.placed_dir), not in which way its own look at the game went: the game
        # closed by now, it is put back here (restore_placed does nothing when drive.run already did).
        if drive.game_pids():
            if plan or drive.anything_left(game):
                print('!!! the game is still running: NOT put back: the placed plugins, the ini, the autopilot' +
                      (f', the range mission (its originals kept in {gen.scenario_backup(game)})' if plan else '') +
                      '. python tools/perfrun.py --restore once the game is closed')
                rc = rc or 1
        else:
            if plan:
                gen.restore_scenario(game)
            if not drive.restore_placed(game):
                print('!!! not everything placed is put back (said above): python tools/perfrun.py --restore '
                      'once it is dealt with')
                rc = rc or 1
    data = run_log(log, log_at)
    if data is not None:
        with open(os.path.join(out, 'vehiclecrew.log'), 'wb') as f:
            f.write(data)
    data = run_log(clog, clog_at)   # only with EDF6Coop installed
    if data:
        with open(os.path.join(out, 'coop.log'), 'wb') as f:
            f.write(data)
    marks = dict(marks)   # a snapshot: the sampling thread may still run when the game ended first
    if not cycle:
        check_wave_window(plan, opts, marks, os.path.join(out, 'vehiclecrew.log'))
    run = dict(scenario=scenario, variant=variant, dll=kept or dll, built=dll, **loaded,
               warm=warm, edf_dll=os.path.join(game, 'EDF.dll'), drive_rc=rc, skipped=skipped, cycle=cycle, **marks)
    with open(os.path.join(out, 'run.json'), 'w', encoding='utf-8') as f:
        json.dump(run, f, indent=1)
    if cycle:
        return rc
    if marks.get('error') or 'windows' not in marks:
        print('sampling failed:', marks.get('error') or f'drive.py ended first (rc {rc})')
        return 1
    return rc


def seconds(text: str, positive: bool = False) -> float:
    """A number of seconds from the command line: finite, not negative (above 0 with `positive`); ValueError
    otherwise (float() takes nan, inf and -5 as well)."""
    v = float(text)
    if not math.isfinite(v) or v < 0 or (positive and v == 0):
        raise ValueError(text)
    return v


def leg_option(text: str) -> dict:
    """One positional argument: SCENARIO[,key=SPEC@S[:MS]]...[,warm=S][,diff=N][,ini=KEY=VALUE]...[,stay] (MS default
    300, as --key's)."""
    scenario, *parts = text.split(',')
    if not scenario:
        raise SystemExit(f'{text}: no scenario before its leg options')
    leg: dict = {'scenario': scenario, 'keys': [], 'warm': None, 'difficulty': None, 'ini': [], 'stay': False}
    for part in parts:
        if part == 'stay':
            leg['stay'] = True
            continue
        name, eq, value = part.partition('=')
        if not eq or not value:
            raise SystemExit(f'{text}: a leg option is NAME=VALUE (key=, warm=, diff=, ini=) or stay, not "{part}"')
        try:
            if name == 'ini':
                if '=' not in value:
                    raise ValueError(value)
                leg['ini'].append(value)
            elif name == 'key':
                import drive
                leg['keys'].append(drive.key_option(value if ':' in value else value + ':300'))
            elif name == 'warm':
                leg['warm'] = seconds(value)
            elif name == 'diff':
                leg['difficulty'] = int(value)
                if not 0 <= leg['difficulty'] <= 4:
                    raise ValueError(value)
            else:
                raise SystemExit(f'{text}: unknown leg option {name} (key=, warm=, diff=, ini=, stay)')
        except ValueError:
            raise SystemExit(f'{text}: bad {name}={value}') from None
    return leg


def leg_names(legs: list[dict]) -> list[str]:
    """Each leg's folder under the run's: the scenario (':' as '_'), -2, -3 ... on one run twice."""
    names: list[str] = []
    for leg in legs:
        base = leg.get('name') or leg['scenario'].replace(':', '_')
        name, k = base, 1
        while name in names:
            k += 1
            name = f'{base}-{k}'
        names.append(name)
    return names


def leg_mission(scenario: str) -> str:
    """The `mission` / `next` name of a leg: range for a range scenario, the stock mission itself otherwise."""
    return 'range' if is_range(scenario) else scenario


def legs_longest(warms: list[float], cpu_s: int, stacks_s: int, leg_timeout: float) -> int:
    """drive.run's SECONDS for a launch of legs (counted from the first mission's PlayMission_Offline, its loading
    still ahead): every leg's sampling, every leg's way in (the first one's loading too) at its longest, and 90 s (as
    one_run's): only the deadline, the run quits as soon as the last leg is sampled."""
    return int(sum(w + cpu_s + stacks_s for w in warms) + len(warms) * leg_timeout + 90)


TIME = re.compile(rb'^\[(?:\d{4}-\d\d-\d\d )?(\d+):(\d+):(\d+(?:\.\d+)?)\]')


def tod(t: float) -> float:
    """Wall-clock `t` as the time of day the logs stamp their rows with (local time)."""
    lt = time.localtime(t)
    return lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec + (t % 1)


def split_log(data: bytes, start: float, bounds: list[float]) -> list[bytes]:
    """One launch's log (rows stamped [HH:MM:SS.sss], EDF6Coop's with the date first) cut at the wall-clock `bounds`
    (ascending: each later leg's `next` handed): len(bounds) + 1 pieces, a leg's leaving and loading in its own piece.
    A row's time is counted from `start` (taken before the launch), so a run past midnight still cuts right (a run is
    under a day; a stamp a little before `start` is clock slop, at `start`); a row without a stamp (a dump's
    continuation) goes with the row before it."""
    base = tod(start)
    pieces: list[list[bytes]] = [[] for _ in range(len(bounds) + 1)]
    k = 0
    for line in data.splitlines(keepends=True):
        m = TIME.match(line)
        if m:
            d = (int(m[1]) * 3600 + int(m[2]) * 60 + float(m[3]) - base) % 86400
            t = start + (0.0 if d > 86400 - 3600 else d)
            k = sum(1 for b in bounds if b <= t)
        pieces[k].append(line)
    return [b''.join(p) for p in pieces]


def hold_leg_keys(game: str, keys: list[tuple[str, float, int]], entered: float, marks: dict, gate) -> None:
    """A leg's key= holds, each S seconds after the leg entered its mission (drive.key: the autopilot's keys file,
    through the run's gate: none once its end began)."""
    import drive
    for spec, at, ms in sorted(keys, key=lambda k: k[1]):
        time.sleep(max(0.0, entered + at - time.time()))
        try:
            drive.key(game, spec, ms, gate)
        except Exception as e:   # noqa: BLE001 - kept for the leg's run.json; the leg's sampling goes on
            marks['key_error'] = f'{spec}@{at:g}: {type(e).__name__}: {e}'
            return
        marks.setdefault('keys_held', []).append([spec, at, ms, round(time.time() - entered - ms / 1000, 1)])


def wait_next(game: str, k: int, seen: int, timeout: float, mission: str, difficulty: int,
              gate=None) -> str | None:
    """Until the launch's k-th mission entry (k 0: the first, its RUNNING row; k 1...: the k-th `next`) runs the
    mission asked (None) or why not: the way in failed (refused, the pack's mode not entered, the script back at the
    title: at once, not after `timeout`), the mission running is another one (its row, difficulty or mode's content id
    not `mission`'s / `difficulty`), the game gone, the run's end begun, or `timeout` seconds passed. The autopilot log
    rows after the first `seen` (those before the ask) are read."""
    import drive
    what = f'next {k}' if k else 'the first mission'
    end = time.time() + timeout
    while time.time() < end:
        how = drive.entry_outcome(drive.rows(game)[seen:], k)
        if how and how[0] == 'failed':
            return f'{what} failed: {how[1]}'
        if how:
            row, content = drive.mission_target(game, mission)
            bad = drive.entry_mismatch(how[1], row, difficulty, content)
            return f'{what} entered another mission than {mission} (difficulty {difficulty}): {bad}' if bad else None
        if not drive.game_pids():
            return f'the game exited before {what} was in its mission'
        if gate is not None and gate.state != drive.Gate.OPEN:
            return f'the run ended before {what} was in its mission'
        time.sleep(0.5)
    return f'{what} not in its mission {timeout:.0f} s after it was asked (--leg-timeout)'


def run_legs(game: str, legs: list[dict], plans: list, warms: list[float], difficulties: list[int], dirs: list[str],
             legmarks: list[dict], skipped: list, state: dict, opts: dict) -> None:
    """drive.run's `during` for legs: the first leg is in its mission when this starts; each leg sampled into its
    folder (its keys on a thread of their own), then the next one's range files and its `next`. The first failure ends
    the run (kept in its leg's marks['error']; returning ends drive.run's stay)."""
    import drive
    import gen
    gate = state['gate']
    i = 0
    try:
        for i, leg in enumerate(legs):
            began = time.monotonic()
            marks = legmarks[i]
            mission = leg_mission(leg['scenario'])
            seen = 0   # the first: every row of the launch (its `running` went with the mission)
            if leg['stay']:   # the mission the leg before is in: its ini only, reloaded by the plugin
                mark = log_mark(plugin_log(game))
                with gate.writing():
                    drive.replace_placed(game, INI_DEST, drive.ini_text_with(state['ini'], tuple(leg['ini'])))
                marks['asked'] = time.time()
                why = wait_reload(plugin_log(game), mark, RELOAD_TIMEOUT, gate)
                if why:
                    marks['error'] = why
                    return
                marks['entered'] = time.time()
                _sample(dirs[i], warms[i], opts['cpu'], 0, opts['interval'], marks, opts.get('presentmon'))
                if marks.get('error') or 'windows' not in marks:
                    marks.setdefault('error', 'not sampled')
                    return
                marks['done'] = time.time()
                continue
            if i:
                if leg['ini'] or legs[i - 1]['ini']:   # this leg's settings (or the launch's again) before its way in
                    with gate.writing():
                        drive.replace_placed(game, INI_DEST, drive.ini_text_with(state['ini'], tuple(leg['ini'])))
                seen = len(drive.rows(game))
                # the range's files and the `next`, each checked and written under the run's gate: nothing after the
                # run's quit (it would overwrite it) or once it puts things back (left behind) -- drive.Gate
                if plans[i]:
                    with gate.writing():
                        skipped[i] = (gen.rewrite_scenario if state['written'] else gen.write_scenario)(game, plans[i])
                        state['written'] = True
                marks['asked'] = time.time()
                drive.command(game, drive.next_command(mission, difficulties[i], opts['next_reboot']), gate)
            # in its mission once the mission runs (the loading over), every leg the same way: its warm-up from there
            why = wait_next(game, i, seen, opts['leg_timeout'], mission, difficulties[i], gate)
            if why:
                marks['error'] = why
                return
            marks['entered'] = time.time()
            keys = threading.Thread(target=hold_leg_keys, args=(game, leg['keys'], marks['entered'], marks, gate),
                                    daemon=True)
            keys.start()
            if needs_population(plans[i], opts):
                why = wait_population(game, plugin_log(game), opts['leg_timeout'] - (time.monotonic() - began), gate,
                                      population_want(plans[i]))
                if why:
                    marks['error'] = why
                    return
                marks['population_ready'] = time.time()
            _sample(dirs[i], warms[i], opts['cpu'], opts['stacks'], opts['interval'], marks, opts.get('presentmon'))
            keys.join()
            if marks.get('error') or 'windows' not in marks:
                marks.setdefault('error', 'not sampled')
                return
            marks['done'] = time.time()
    except Exception as e:   # noqa: BLE001 - kept for the run's verdict; the run must still quit and restore
        legmarks[i]['error'] = f'{type(e).__name__}: {e}'


INI_DEST = 'Plugins/EDF6VehicleCrew.ini'   # drive.run's --ini placement, rewritten for a leg's ini=


def multi_run(game: str, legs: list[dict], out: str, dll: str | None, opts: dict) -> int:
    """One launch of the game for all `legs` in order: each leg's run.json, logs and samples in <out>/<leg>/, the
    launch's whole logs in `out`; everything put back once, at the end. Non-zero when a leg was not sampled, the run
    failed, or not everything could be put back."""
    import drive
    import gen
    if drive.game_pids():
        raise SystemExit('the game is running (the user may be playing): not touched')
    plans = [plan_for(leg['scenario']) for leg in legs]
    if any(plans):
        if gen.installed(game) and os.path.exists(gen.scenario_backup(game)):
            raise SystemExit(f'上一次的测试场原件还留在 {gen.scenario_backup(game)}：先 python tools/perfrun.py --restore')
        for plan in plans:   # every range leg's checks before the launch, not found out half way through it
            if plan:
                gen.check_scenario(game, plan)
    names = leg_names(legs)
    dirs = [os.path.join(out, n) for n in names]
    for d in dirs:
        os.makedirs(d, exist_ok=True)
    warms = [(leg['warm'] if leg['warm'] is not None else STAY_SETTLE) if leg['stay'] else
             warm_for(leg['scenario'], leg['warm'] if leg['warm'] is not None else opts['warm']) for leg in legs]
    difficulties = [leg['difficulty'] if leg['difficulty'] is not None else opts['difficulty'] for leg in legs]
    legmarks: list[dict] = [{} for _ in legs]
    skipped: list = [[] for _ in legs]
    launch_ini = ('Debug=1', 'PerfProbe=1', *opts['ini'])
    # the launch's ini (before drive.run places it, from the game's own): a later leg's ini= go over it
    state = {'gate': drive.Gate(), 'written': False, 'ini': drive.ini_with(game, launch_ini)}
    log = plugin_log(game)
    log_at = log_mark(log)
    clog = coop_log(game)
    clog_at = log_mark(clog)
    loaded = loaded_plugins(game, dll)
    kept = keep_symbols(dll, out)
    start = time.time()
    if plans[0]:
        skipped[0] = gen.write_scenario(game, plans[0])
        state['written'] = True
    rc = 1
    try:
        rc = drive.run(game, leg_mission(legs[0]['scenario']), difficulties[0],
                       legs_longest(warms, opts['cpu'], opts['stacks'], opts['leg_timeout']),
                       os.path.join(out, 'autopilot.log'), place=placements(dll),
                       ini=(*launch_ini, *legs[0]['ini']),
                       shots=os.path.join(out, 'shots') if opts['shots'] else None,
                       during=lambda: run_legs(game, legs, plans, warms, difficulties, dirs, legmarks, skipped, state,
                                               opts),
                       quit_wait=600, extra=('running',), gate=state['gate'])
    finally:
        state['gate'].end(drive.Gate.CLOSED)   # drive.run did already, unless it never got that far
        if drive.game_pids():
            if state['written'] or drive.anything_left(game):
                print('!!! the game is still running: NOT put back: the placed plugins, the ini, the autopilot' +
                      (f', the range mission (its originals kept in {gen.scenario_backup(game)})'
                       if state['written'] else '') + '. python tools/perfrun.py --restore once the game is closed')
                rc = rc or 1
        else:
            if state['written']:
                gen.restore_scenario(game)
            if not drive.restore_placed(game):
                print('!!! not everything placed is put back (said above): python tools/perfrun.py --restore '
                      'once it is dealt with')
                rc = rc or 1
    legmarks = [dict(m) for m in legmarks]   # snapshots: the legs' thread may still run when the game ended first
    reached = [m['asked'] for m in legmarks[1:] if 'asked' in m]
    for path, at, name in ((log, log_at, 'vehiclecrew.log'), (clog, clog_at, 'coop.log')):
        data = run_log(path, at)
        if not data:
            continue
        with open(os.path.join(out, name), 'wb') as f:
            f.write(data)
        for d, piece in zip(dirs, split_log(data, start, reached)):
            with open(os.path.join(d, name), 'wb') as f:
                f.write(piece)
    for i, (leg, d, marks) in enumerate(zip(legs, dirs, legmarks)):
        check_wave_window(plans[i], opts, marks, os.path.join(d, 'vehiclecrew.log'))
        run = dict(scenario=leg['scenario'], variant=leg.get('variant', ''), stay=leg['stay'], ini=leg['ini'], leg=i + 1, legs=len(legs), dll=kept or dll, built=dll,
                   **loaded, warm=warms[i], difficulty=difficulties[i], keys=leg['keys'],
                   edf_dll=os.path.join(game, 'EDF.dll'), drive_rc=rc, skipped=skipped[i], cycle=None, **marks)
        with open(os.path.join(d, 'run.json'), 'w', encoding='utf-8') as f:
            json.dump(run, f, indent=1)
    failed = [(names[i], m) for i, m in enumerate(legmarks) if 'windows' not in m or m.get('error')]
    for name, marks in failed:
        state = 'invalid sample' if 'windows' in marks else 'not sampled'
        print(f'leg {name} {state}: {marks.get("error") or "not reached"}')
    return rc or (1 if failed else 0)


PLUGIN_OWN = re.compile(r"the plugin's own work \(not the game's own under a hook that only calls through\): ([\d.]+)%")


def legs_table(dirs: list[str]) -> str:
    """summary.md: a row a leg, in order: its frames (mean, a second, over 20 ms, steps a frame), the plugin's own work
    on the game thread (the leg's report.md), the living objects, the process CPU; why a leg has no numbers. A leg's
    error (its run.json's error / key_error: a stack window that failed after the clean one, a key not held) is in its
    own last column, **ERROR** first, its numbers kept but not to be taken as a clean leg's."""
    import perf_report
    out = ['| leg | scenario | warm s | mean ms | fps | over 20 ms | steps a frame | plugin own work | living objects | '
           'process CPU (% of a core) | error |', '|---|---|---|---|---|---|---|---|---|---|---|']
    for d in dirs:
        name = os.path.basename(d)
        try:
            with open(os.path.join(d, 'run.json'), encoding='utf-8') as f:
                run = json.load(f)
        except OSError:
            run = {}
        scenario = run.get('scenario', '?')
        warm = f'{run["warm"]:.0f}' if 'warm' in run else ''
        errors = [run[k] for k in ('error', 'key_error') if run.get(k)]
        pop = run.get('population') or {}
        if pop and not pop.get('held'):   # bench: the window did not hold E (its numbers are another size's)
            errors.append(f'population not held: {pop.get("mean", 0):.0f} of {pop.get("want")} (max {pop.get("max")})')
        err = ('**ERROR** ' + '; '.join(errors).replace('|', '/')) if errors else ''
        if 'cpu' not in run.get('windows', {}):
            why = (run.get('error') or 'not reached').replace('|', '/')
            err = err or '**ERROR** not sampled'
            out.append(f'| {name} | {scenario} | {warm} | not sampled: {why} | | | | | | | {err} |')
            continue
        cw = tuple(run['windows']['cpu'])
        log = os.path.join(d, 'vehiclecrew.log')
        fs = perf_report.frame_summary(perf_report.read_perf(log, cw))
        objs = perf_report.read_objects(log, cw)
        cpu = perf_report.read_cpu(os.path.join(d, 'cpu.txt')) if os.path.exists(os.path.join(d, 'cpu.txt')) else {}
        secs = cw[1] - cw[0]
        own = ''
        try:
            with open(os.path.join(d, 'report.md'), encoding='utf-8') as f:
                m = PLUGIN_OWN.search(f.read())
            own = f'{m[1]}%' if m else ''
        except OSError:
            pass
        c = f'{cpu.get("total", sum(cpu.values())) / 10 / secs:.0f}' if cpu else ''
        if not fs:
            out.append(f'| {name} | {scenario} | {warm} | no PERF rows | | | | {own} | '
                       f'{perf_report.objects_text(objs)} | {c} | {err} |')
            continue
        o20 = f'{fs["over20_pct"]:.2f}%' if fs['over20_pct'] is not None else 'n/a'
        spf = f'{fs["steps_per_frame"]:.2f}' if fs['steps_per_frame'] is not None else 'n/a'
        out.append(f'| {name} | {scenario} | {warm} | {fs["mean_ms"]:.2f} | {fs["fps"]:.1f} | {o20} | {spf} | {own} | '
                   f'{perf_report.objects_text(objs)} | {c} | {err} |')
    return '\n'.join(out) + '\n'


def options(argv: list[str]) -> tuple[str, dict]:
    opts = {'dll': DEFAULT_DLL, 'ab': None, 'rounds': 2, 'warm': None, 'cpu': 20, 'stacks': 20, 'interval': 2,
            'difficulty': 1, 'out': None, 'shots': False, 'restore': False, 'ini': [], 'presentmon': None,
            'cycle': 0, 'cycle_stay': 20.0, 'keys': [], 'legs': [], 'leg_timeout': 900.0, 'next_reboot': False,
            'flip': None, 'normal_priority': False}
    pos = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ('--shots', '--restore', '--next-reboot', '--normal-priority'):
            opts[a[2:].replace('-', '_')] = True
            i += 1
        elif a == '--ab':
            opts['ab'] = (os.path.abspath(argv[i + 1]), os.path.abspath(argv[i + 2]))
            i += 3
        elif a == '--flip':
            opts['flip'] = [p.strip() for p in argv[i + 1].split(';') if p.strip()]
            if not opts['flip'] or any('=' not in p for p in opts['flip']):
                raise SystemExit('--flip wants "KEY=VALUE[;KEY=VALUE]"')
            i += 2
        elif a in ('--dll', '--out', '--presentmon'):
            opts[a[2:]] = argv[i + 1]
            i += 2
        elif a == '--ini':
            if '=' not in argv[i + 1]:
                raise SystemExit('--ini wants KEY=VALUE')
            opts['ini'].append(argv[i + 1])
            i += 2
        elif a == '--key':
            import drive
            spec, at, ms = drive.key_option(argv[i + 1] if ':' in argv[i + 1] else argv[i + 1] + ':300')
            opts['keys'].append((spec, at, ms))
            i += 2
        elif a in ('--warm', '--cycle-stay', '--leg-timeout'):
            try:
                opts[a[2:].replace('-', '_')] = seconds(argv[i + 1], positive=a != '--warm')
            except ValueError:
                raise SystemExit(f'{a} {argv[i + 1]}: a finite number of seconds, not negative') from None
            i += 2
        elif a in ('--rounds', '--cycle', '--cpu', '--stacks', '--interval', '--difficulty'):
            try:
                opts[a[2:]] = int(argv[i + 1])
            except ValueError:
                raise SystemExit(f'{a} {argv[i + 1]}: a whole number') from None
            if opts[a[2:]] < 0 or (a in ('--rounds', '--cpu', '--interval') and opts[a[2:]] == 0) or \
                    (a == '--difficulty' and opts[a[2:]] > 4):
                raise SystemExit(f'{a} {argv[i + 1]}: out of range')
            i += 2
        elif a.startswith('--'):
            raise SystemExit(f'unknown option {a}')
        else:
            pos.append(a)
            i += 1
    if opts['restore'] if pos else not opts['restore']:
        raise SystemExit(__doc__)
    legs = [leg_option(p) for p in pos]
    if legs and legs[0]['stay']:
        raise SystemExit(f'{pos[0]}: the first leg has no mission to stay in')
    if opts['flip']:
        if opts['ab'] or opts['cycle']:
            raise SystemExit('--flip is an A/B in one launch: not with --ab or --cycle')
        legs = flip_legs(legs, opts['flip'], opts['rounds'])
        if '--cpu' not in argv:
            opts['cpu'] = FLIP_CPU
    if len(legs) > 1:
        for bad, why in ((opts['ab'], '--ab'), (opts['cycle'], '--cycle'), (opts['keys'], '--key (key= on a leg)')):
            if bad:
                raise SystemExit(f'{why} is not for a run of legs: one scenario only')
    elif legs:   # one scenario: as before, its leg options as the run's
        leg = legs[0]
        opts['keys'] += leg['keys']
        if leg['warm'] is not None:
            opts['warm'] = leg['warm']
        if leg['difficulty'] is not None:
            opts['difficulty'] = leg['difficulty']
    opts['legs'] = legs
    if opts['dll'] == 'installed':
        opts['dll'] = None
    elif opts['dll']:
        opts['dll'] = os.path.abspath(opts['dll'])
        if not os.path.isfile(opts['dll']):
            raise SystemExit(f'no DLL at {opts["dll"]} (build it, or --dll installed)')
    if opts['presentmon']:
        opts['presentmon'] = os.path.abspath(opts['presentmon'])
        if not os.path.isfile(opts['presentmon']):
            raise SystemExit(f'no PresentMon at {opts["presentmon"]}')
    if opts['cycle'] and opts['ab']:
        raise SystemExit('--cycle is a load run: not with --ab')
    return (legs[0]['scenario'] if legs else ''), opts


def flip_legs(legs: list[dict], flip: list[str], rounds: int) -> list[dict]:
    """--flip: each leg as A, then 2 x `rounds` - 1 stay legs of it, B (`flip` over its ini=) and A by turns."""
    out = []
    for leg in legs:
        base = leg['scenario'].replace(':', '_')
        out.append({**leg, 'variant': 'A', 'name': f'{base}-A1'})
        for k in range(1, 2 * rounds):
            b = k % 2 == 1
            out.append({'scenario': leg['scenario'], 'keys': [], 'warm': None, 'difficulty': leg['difficulty'],
                        'ini': [*leg['ini'], *flip] if b else list(leg['ini']), 'stay': True,
                        'variant': 'B' if b else 'A', 'name': f'{base}-{"B" if b else "A"}{k // 2 + 1}'})
    return out


def loads_report(out: str) -> str:
    """A load run's loads.md: each entry of the launch (perf_report.read_loads), cold first."""
    import perf_report
    with open(os.path.join(out, 'run.json'), encoding='utf-8') as f:
        run = json.load(f)
    text = '\n'.join([f'# {run["scenario"]}: left and entered again {run["cycle"]} times', '',
                      *perf_report.loads_text(perf_report.read_loads(os.path.join(out, 'vehiclecrew.log')))]) + '\n'
    with open(os.path.join(out, 'loads.md'), 'w', encoding='utf-8') as f:
        f.write(text)
    return text


def main(argv: list[str]) -> int:
    scenario, opts = options(argv)
    game = gamedir.find_or_dev()
    import drive
    import gen
    import perf_report
    drive.BELOW_NORMAL = not opts['normal_priority']
    if opts['restore']:
        with gamelease.lease(game, 'perfrun --restore'):
            if drive.game_pids():
                raise SystemExit('the game is running: close it first')
            print('the range mission put back' if gen.restore_scenario(game) else 'no range mission kept to put back')
            whole = drive.restore_placed(game)   # the placed DLLs and ini, the autopilot
        return 0 if whole else 1
    for path, target in ((SAMPLER, 'rip_sampler'), (AUTOPILOT, 'EDF6Autopilot')):
        if not os.path.isfile(path):   # both EXCLUDE_FROM_ALL: build.cmd alone does not make them
            raise SystemExit(f'no {path}: cmake --build build --target {target}')
    stamp = time.strftime('%m%d-%H%M%S')
    if len(opts['legs']) > 1:
        out = os.path.abspath(opts['out'] or os.path.join(ROOT, 'tmp', 'perf', f'legs-{stamp}'))
        with gamelease.lease(game, 'perfrun ' + ' '.join(argv)):
            rc = multi_run(game, opts['legs'], out, opts['dll'], opts)
        dirs = [os.path.join(out, n) for n in leg_names(opts['legs'])]
        for d in dirs:
            with open(os.path.join(d, 'run.json'), encoding='utf-8') as f:
                if 'cpu' in json.load(f).get('windows', {}):
                    perf_report.report(d)
                    print('report:', os.path.join(d, 'report.md'))
        table = legs_table(dirs)
        variants = [leg.get('variant', '') for leg in opts['legs']]
        for v in sorted(set(variants)):   # a scaling table a variant (--flip: A and B each), not the legs mixed
            scaling = perf_report.scaling_table([d for d, w in zip(dirs, variants) if w == v])
            if scaling:
                table += '\n' + (scaling.replace('## Scaling', f'## Scaling, {v}', 1) if v else scaling)
        if opts['flip']:
            table += '\n' + perf_report.flip_table(dirs, opts['flip'])
        with open(os.path.join(out, 'summary.md'), 'w', encoding='utf-8') as f:
            f.write(table)
        print(table)
        print('saved in', out)
        return rc
    out = os.path.abspath(opts['out'] or os.path.join(ROOT, 'tmp', 'perf', f'{scenario.replace(":", "_")}-{stamp}'))
    with gamelease.lease(game, 'perfrun ' + ' '.join(argv)):
        if opts['ab']:
            why = ab_turrets(game, *opts['ab'])
            if why:
                raise SystemExit(why)
            opts['stacks'] = 0
            dirs = []
            rc = 0
            for k in range(opts['rounds']):
                for name, dll in zip('AB', opts['ab']):
                    d = os.path.join(out, f'{k}{name}')
                    print(f'--- round {k + 1}/{opts["rounds"]} {name}: {dll}', flush=True)
                    rc = one_run(game, scenario, d, dll, opts, variant=name)
                    dirs.append(d)
                    if rc:
                        print(f'round {k + 1} {name} failed (exit {rc}): stopping the A/B here')
                        break
                if rc:
                    break
            table = perf_report.ab_table(dirs)
            with open(os.path.join(out, 'ab.md'), 'w', encoding='utf-8') as f:
                f.write(table)
            print(table)
            print('saved in', out)
            return rc
        rc = one_run(game, scenario, out, opts['dll'], opts)
    if opts['cycle']:
        print(loads_report(out))
        print('saved in', out)
        return rc
    with open(os.path.join(out, 'run.json'), encoding='utf-8') as f:
        sampled = 'windows' in json.load(f)
    if sampled:
        print(perf_report.report(out))
    print('saved in', out)
    return rc


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
