"""Measure the game in one go: a scenario in the background, sampled once it is busy, a report, everything put back.

  python tools/perfrun.py SCENARIO [options]

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
  --shots           a picture of the game window every 15 s (PrintWindow: it costs frames, keep it off to measure)
  --restore         put back what a run could not (the game outlived it): the range mission's files, and every file
                    drive.py placed (the DLLs, the ini) byte for byte with the autopilot removed (drive.py restore)

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
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
for sub in ('pylib', 'testrange', os.path.join('tests', 'autopilot'), 'tools'):
    sys.path.insert(0, os.path.join(ROOT, sub))
import gamedir  # noqa: E402
import gamelease  # noqa: E402

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


def sample(out: str, warm: float, cpu_s: int, stacks_s: int, interval: int, marks: dict) -> None:
    """The run's `during`: wait `warm`, then the clean window (rip_sampler interval 0 and typeperf side by side), then
    the stack window. A failure is kept in marks['error'] (and ends the stay, as any return does)."""
    try:
        _sample(out, warm, cpu_s, stacks_s, interval, marks)
    except Exception as e:   # noqa: BLE001 - kept for the run's verdict; the run must still quit and restore
        marks['error'] = f'{type(e).__name__}: {e}'


def _sample(out: str, warm: float, cpu_s: int, stacks_s: int, interval: int, marks: dict) -> None:
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
    r = subprocess.run([SAMPLER, str(pid), str(cpu_s), '0', os.path.join(out, 'cpu.txt')], capture_output=True, text=True)
    end = time.time()
    if r.returncode:
        marks['error'] = f'rip_sampler (CPU window) exit {r.returncode}: {r.stderr.strip()[-200:]}'
        return
    # rip_sampler times its own `seconds` after attaching (SymInitialize first) and exits right after: the window is
    # the last cpu_s seconds before it returned, not from its start
    marks['windows'] = {'cpu': [end - cpu_s, end]}
    try:
        gpu.wait(timeout=15)
    except subprocess.TimeoutExpired:
        gpu.kill()
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


def one_run(game: str, scenario: str, out: str, dll: str | None, opts: dict, variant: str = '') -> int:
    """One launch of the game: the scenario, sampled, its run.json and logs in `out`; everything put back. Non-zero
    when nothing was sampled, the run failed, or the range's files could not be put back (the game still running)."""
    import drive
    import gen
    if drive.game_pids():
        raise SystemExit('the game is running (the user may be playing): not touched')
    os.makedirs(out, exist_ok=True)
    plan = plan_for(scenario)
    kind, *rest = scenario.split(':')
    warm = (opts['warm'] if opts['warm'] is not None else
            stress_fields(int(rest[0]) if rest and rest[0] else 1)[2] if kind == 'stress' else WARM.get(kind, 25.0))
    marks: dict = {}
    log = plugin_log(game)
    # the plugin appends to its log across launches: only what this launch writes is this run's
    log_at = log_mark(log)
    loaded = loaded_plugins(game, dll)   # before the launch: the files the game is about to load
    skipped = gen.write_scenario(game, plan) if plan else []
    rc = 1
    try:
        place = placements(dll)
        longest = int(warm + opts['cpu'] + opts['stacks'] + 90)
        # Debug=1 is the shipped default (EDF6VehicleCrew.ini): the PERF rows, and what players run
        rc = drive.run(game, 'range' if plan else scenario, opts['difficulty'], longest,
                       os.path.join(out, 'autopilot.log'), place=place, ini=('Debug=1', *opts['ini']),
                       shots=os.path.join(out, 'shots') if opts['shots'] else None,
                       during=lambda: sample(out, warm, opts['cpu'], opts['stacks'], opts['interval'], marks),
                       quit_wait=600)   # stress:100 at 8 fps was still leaving the mission after drive's 90 s
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
    marks = dict(marks)   # a snapshot: the sampling thread may still run when the game ended first
    run = dict(scenario=scenario, variant=variant, dll=dll, **loaded,
               warm=warm, edf_dll=os.path.join(game, 'EDF.dll'), drive_rc=rc, skipped=skipped, **marks)
    with open(os.path.join(out, 'run.json'), 'w', encoding='utf-8') as f:
        json.dump(run, f, indent=1)
    if marks.get('error') or 'windows' not in marks:
        print('sampling failed:', marks.get('error') or f'drive.py ended first (rc {rc})')
        return 1
    return rc


def options(argv: list[str]) -> tuple[str, dict]:
    opts = {'dll': DEFAULT_DLL, 'ab': None, 'rounds': 2, 'warm': None, 'cpu': 20, 'stacks': 20, 'interval': 2,
            'difficulty': 1, 'out': None, 'shots': False, 'restore': False, 'ini': []}
    pos = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ('--shots', '--restore'):
            opts[a[2:]] = True
            i += 1
        elif a == '--ab':
            opts['ab'] = (os.path.abspath(argv[i + 1]), os.path.abspath(argv[i + 2]))
            i += 3
        elif a in ('--dll', '--out'):
            opts[a[2:]] = argv[i + 1]
            i += 2
        elif a == '--ini':
            if '=' not in argv[i + 1]:
                raise SystemExit('--ini wants KEY=VALUE')
            opts['ini'].append(argv[i + 1])
            i += 2
        elif a == '--warm':
            opts['warm'] = float(argv[i + 1])
            i += 2
        elif a in ('--rounds', '--cpu', '--stacks', '--interval', '--difficulty'):
            opts[a[2:]] = int(argv[i + 1])
            i += 2
        elif a.startswith('--'):
            raise SystemExit(f'unknown option {a}')
        else:
            pos.append(a)
            i += 1
    if len(pos) != (0 if opts['restore'] else 1):
        raise SystemExit(__doc__)
    if opts['dll'] == 'installed':
        opts['dll'] = None
    elif opts['dll']:
        opts['dll'] = os.path.abspath(opts['dll'])
        if not os.path.isfile(opts['dll']):
            raise SystemExit(f'no DLL at {opts["dll"]} (build it, or --dll installed)')
    return (pos[0] if pos else ''), opts


def main(argv: list[str]) -> int:
    scenario, opts = options(argv)
    game = gamedir.find_or_dev()
    import drive
    import gen
    import perf_report
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
    with open(os.path.join(out, 'run.json'), encoding='utf-8') as f:
        sampled = 'windows' in json.load(f)
    if sampled:
        print(perf_report.report(out))
    print('saved in', out)
    return rc


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
