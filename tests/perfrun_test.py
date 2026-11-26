"""tools/perfrun.py and tools/perf_report.py, offline (no game):
  - testrange/gen.py write_scenario rewrites only the range mission's own files, keeping the originals on disk (outside
    the game folder) so a run that dies loses nothing, and restore_scenario puts them back byte for byte (a file that
    was not there is removed again); it refuses, before writing anything, when the range is not installed, the waves'
    enemy is not one the range knows, a generated SGO the plan spawns is not in Mods, or an earlier run's originals
    are still kept (a second write would take the first one's scenario for the original); a copy of the originals
    that fails leaves no half backup (the next run goes ahead);
  - perfrun's scenarios: grand is the grand battle, swarm's fields reach the waves, a stock mission has no plan;
  - perf_report reads the plugin's PERF rows only inside the window (both row formats), sums GPU engines per type, and
    tells the game thread by System::update on its stack, labels a plugin hook below a phase by its outermost
    plugin frame (not the phase's own time) and ranks the plugin's functions by inclusive and self samples;
  - drive.run's `during` ends the stay as soon as it returns (the loop condition, read from the source).
  - the measurement probes' rows (made from the plugin's own format strings): frames and steps, the frame split and
    its n = 0 floor, the render thread, the channels, SLOW FRAME and the samples inside one by QPC, the mission entries
    cold and warm, PresentMon, EDF6Coop STATS; the thread roles and phase buckets; perfrun's --cycle and --presentmon.
  - legs (several scenarios in one launch): the positional arguments with their key= / warm= / diff=, each leg's
    warm-up, its `next` command, the autopilot's NEXT rows, the launch's logs cut a leg each (past midnight too),
    gen.rewrite_scenario keeping the first originals, one launch of three legs with drive.run faked (the range written
    once before the launch, rewritten for the later range leg, put back once; the keys timed from the leg's entry; a
    leg that never comes in ends the run), and summary.md's row a leg.
Exit code 1 when one fails.
"""
from __future__ import annotations

import inspect
import json
import os
import sys
import tempfile
import time

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
sys.path[:0] = [os.path.join(ROOT, 'tools'), os.path.join(ROOT, 'pylib'), os.path.join(ROOT, 'testrange'),
                os.path.join(ROOT, 'tests', 'autopilot')]
import gen  # noqa: E402
import perf_report  # noqa: E402
import perfrun  # noqa: E402

failures: list[str] = []


def check(ok: bool, what: str) -> None:
    print(('ok   ' if ok else 'FAIL ') + what)
    if not ok:
        failures.append(what)


def scenario_files() -> None:
    with tempfile.TemporaryDirectory() as root, tempfile.TemporaryDirectory() as queues:
        os.environ['EDF6_QUEUE_DIR'] = queues
        out = gen.mission_dir(root, gen.RANGE_MISSION)
        os.makedirs(out)
        with open(os.path.join(out, 'MISSION.AC'), 'wb') as f:
            f.write(b'old script')
        with open(os.path.join(out, gen.MARKER), 'wb') as f:
            f.write(b'marker')
        saved = {k: getattr(gen, k) for k in ('installed', 'Game', '_range_files', 'without_missing', 'has_mission_setup')}
        written = []

        def fake_write(game_root, game, mission, site, text, points):
            written.append(mission)
            for name, data in (('MISSION.AC', text.encode()), ('MISSION.RMPA', points), ('MISSION.JSON', b'{}'),
                               (gen.MARKER, b'marker')):
                with open(os.path.join(gen.mission_dir(game_root, mission), name), 'wb') as f:
                    f.write(data)
        gen.installed = lambda game_root: True
        gen.Game = lambda game_root: None
        gen.without_missing = lambda game_root, plan: (plan, [])
        gen.has_mission_setup = lambda game, name: True
        gen._range_files = lambda game_root, game, plan: ('new script', b'points', [], False)
        real_write, gen._write_mission = gen._write_mission, fake_write
        try:
            plan = gen.Plan()
            plan.vehicles = {}
            plan.waves.enabled = False
            gen.write_scenario(root, plan)
            check(written == [gen.RANGE_MISSION], 'write_scenario writes the range mission only')
            backup = gen.scenario_backup(root)
            check(os.path.isdir(backup) and not os.path.abspath(backup).startswith(os.path.abspath(root)),
                  'the originals are kept on disk, outside the game folder')
            with open(os.path.join(backup, 'MISSION.AC'), 'rb') as f:
                check(f.read() == b'old script', 'the kept original is the old script')
            written.clear()
            try:
                gen.write_scenario(root, plan)
                check(False, 'a second write while the originals are kept is refused')
            except RuntimeError:
                check(not written, 'a second write while the originals are kept is refused, before writing')
            with open(os.path.join(out, 'MISSION.AC'), 'rb') as f:
                check(f.read() == b'new script', 'the new script is in place during the run')
            check(gen.restore_scenario(root), 'restore reports it put the files back')
            with open(os.path.join(out, 'MISSION.AC'), 'rb') as f:
                check(f.read() == b'old script', 'restore puts the old script back byte for byte')
            check(not os.path.exists(os.path.join(out, 'MISSION.RMPA')), 'restore removes a file that was not there')
            check(sorted(os.listdir(out)) == sorted(['MISSION.AC', gen.MARKER]), 'nothing else left in the folder')
            check(not os.path.exists(backup) and not gen.restore_scenario(root), 'the kept originals go once restored')
            # a copy that fails while keeping the originals: no half backup left, nothing written, the next run goes
            real_copy = gen.shutil.copy2

            def failing_copy(src, dst, *a, **k):
                raise OSError('disk full')
            gen.shutil.copy2 = failing_copy
            written.clear()
            try:
                gen.write_scenario(root, plan)
                check(False, 'a failed copy of the originals is raised')
            except OSError:
                check(not written, 'a failed copy of the originals writes nothing')
            finally:
                gen.shutil.copy2 = real_copy
            check(not os.path.exists(backup) and not os.path.exists(backup + '.tmp'),
                  'a failed copy of the originals leaves no half backup')
            gen.write_scenario(root, plan)
            check(written == [gen.RANGE_MISSION] and os.path.isfile(os.path.join(backup, 'absent.json')),
                  'the run after a failed copy goes ahead with a whole backup')
            check(gen.restore_scenario(root), 'and is restored')
            with open(os.path.join(out, 'MISSION.AC'), 'rb') as f:
                check(f.read() == b'old script', 'the old script back after the failed copy and the next run')
            written.clear()
            bad = gen.Plan()
            bad.vehicles = {}
            bad.waves.enemy = 'giantant1'
            try:
                gen.write_scenario(root, bad)
                check(False, 'an enemy the range does not know is refused')
            except RuntimeError:
                check(not written and not os.path.exists(backup), 'an enemy the range does not know is refused, '
                      'before writing')
            # refusals, nothing written
            gen.installed = lambda game_root: False
            written.clear()
            try:
                gen.write_scenario(root, plan)
                check(False, 'refuses on a range that is not installed')
            except RuntimeError:
                check(not written, 'refuses on a range that is not installed, before writing')
            gen.installed = lambda game_root: True
            jet = next(iter(gen.JETS))
            plan.friends = {jet: 1}
            spawned = gen.spawned(plan) & (gen.DERIVED.keys() | gen.JETS.keys())
            if spawned:
                try:
                    gen.write_scenario(root, plan)
                    check(False, 'refuses when a generated SGO the plan spawns is not in Mods')
                except RuntimeError:
                    check(not written, 'refuses when a generated SGO is missing, before writing')
        finally:
            gen._write_mission = real_write
            for k, v in saved.items():
                setattr(gen, k, v)


def scenarios() -> None:
    check(perfrun.plan_for('M001') is None and perfrun.plan_for('17') is None, 'a stock mission has no range plan')
    check(perfrun.plan_for('grand').scenario == gen.GRAND, 'grand is the grand battle')
    p = perfrun.plan_for('swarm:giantspider01:30:200:0.5')
    check((p.waves.enemy, p.waves.per_wave, p.waves.max_alive, p.waves.level) == ('giantspider01', 30, 200, 0.5)
          and p.waves.enabled, "swarm's fields reach the waves")
    p = perfrun.plan_for('swarm')
    check((p.waves.enemy, p.waves.per_wave, p.waves.max_alive) == ('giantant01', 25, 160), 'swarm defaults')
    p = perfrun.plan_for('blast')
    check(p.waves.max_alive == 300 and p.friends.get('edf6tr_katyusha_mission') == 2 and p.squads == 4,
          'blast: 300 ants against tanks, rockets and artillery')
    p = perfrun.plan_for('horde::30')
    check((p.waves.enemy, p.waves.per_wave, p.waves.max_alive) == ('giantant01', 30, 400),
          "horde: a field given after it overrides the preset's, the rest stay")
    p = perfrun.plan_for('stress:100')
    check((p.waves.max_alive, p.waves.per_wave, p.squads) == (16000, 400, 300)
          and p.friends == perfrun.PRESETS['blast'][1],
          'stress:100: a hundred times the enemies, waves at most 400, blast shelling them, squads capped')
    check(perfrun.plan_for('stress:10').squads == 30, 'stress: 3*N squads below the cap')
    check(perfrun.plan_for('stress:1:giantspider01').waves.enemy == 'giantspider01' and
          perfrun.stress_fields(1)[2] == 40.0 and perfrun.stress_fields(100)[2] == 90.0, 'stress: its enemy, its warm-up')
    for name, (wave, friends, _) in perfrun.PRESETS.items():
        check(wave.split(':')[0] in {s for s, _, _ in gen.ENEMIES} and all(f in gen.DERIVED or f in gen.JETS or
              f.startswith('vehicle') for f in friends), f'preset {name}: an enemy and friends the range knows')


def report_parsing() -> None:
    with tempfile.TemporaryDirectory() as d:
        t0 = time.mktime((2026, 11, 7, 13, 0, 0, 0, 0, -1))
        log = os.path.join(d, 'vehiclecrew.log')
        with open(log, 'w', encoding='utf-8') as f:
            f.write('[13:00:04.000] PERF 300 frames: mean 16.7 ms (60 fps), worst 20.0 ms, 1 over 20 ms, 0 over 33 ms\n'
                    '[13:00:15.000] PERF 300 frames: mean 16.7 ms (60 fps), worst 30.0 ms, 6 over 20 ms, 0 over 33 ms\n'
                    '[13:00:20.000] PERF 200 frames: mean 25.0 ms (40 fps), worst 50.0 ms, 50 over 20 ms, 3 over 33 ms\n'
                    '[13:00:40.000] PERF 300 frames: mean 16.7 ms (60 fps), worst 99.0 ms, 9 over 20 ms, 9 over 33 ms\n')
        rows = perf_report.read_perf(log, (t0 + 10, t0 + 20))
        check([r[1] for r in rows] == [300, 200], 'PERF rows inside the window only')
        s = perf_report.frame_summary(rows)
        check(abs(s['mean_ms'] - (300 * 16.7 + 200 * 25.0) / 500) < 1e-9 and abs(s['over20_pct'] - 56 / 500 * 100) < 1e-9
              and s['over33'] == 3, 'frame summary weights the rows by frames')
        with open(log, 'w', encoding='utf-8') as f:
            f.write('[13:00:15.000] PERF 301 frames: mean 16.7 ms (60 fps), worst 33.3 ms, 0 over 33 ms\n')
        s = perf_report.frame_summary(perf_report.read_perf(log, (t0 + 10, t0 + 20)))
        check(s['frames'] == 301 and s['over20_pct'] is None, 'an older build without "over 20 ms" still reads')
        with open(log, 'w', encoding='utf-8') as f:
            f.write('[13:00:04.000] PERF OBJECTS enemies 9999, player team 1, friends 1\n'
                    '[13:00:15.000] PERF OBJECTS enemies 1000, player team 40, friends 300\n'
                    '[13:00:20.000] PERF OBJECTS enemies 2000, player team 40, friends 280\n')
        o = perf_report.read_objects(log, (t0 + 10, t0 + 20))
        check(o == {'enemies': (1500, 2000), 'player team': (40, 40), 'friends': (290, 300)},
              'PERF OBJECTS: mean and max in the window only')
        check(perf_report.read_objects(os.path.join(d, 'none.log'), (t0, t0 + 1)) == {}, 'no log: no objects')
        gpu = os.path.join(d, 'gpu.csv')
        with open(gpu, 'w', encoding='utf-8') as f:
            f.write('"time","\\\\H\\GPU Engine(pid_1_luid_0_phys_0_eng_0_engtype_3D)\\Utilization Percentage",'
                    '"\\\\H\\GPU Engine(pid_1_luid_0_phys_0_eng_1_engtype_3D)\\Utilization Percentage",'
                    '"\\\\H\\GPU Engine(pid_1_luid_0_phys_0_eng_2_engtype_Copy)\\Utilization Percentage"\n'
                    '"t1","40","10","0"\n"t2","60","30","0"\n')
        g = perf_report.gpu_summary(gpu)
        check(abs(g['3D'][0] - 50) < 1e-9 and 'Copy' not in g,
              'GPU: the busiest engine of a type per sample (as Task Manager), idle types left out')
        # a game thread: System::update on most stacks
        img = perf_report.Image.__new__(perf_report.Image)
        img.names = {}
        img.pdata = [(0x500000, 0x500100), (perf_report.SYSTEM_UPDATE, perf_report.SYSTEM_UPDATE + 0x1000)]
        img.starts = [b for b, _ in img.pdata]
        img.slot_class = {0x500000: 'GiantAnt#5'}
        stacks = [['ntdll.dll+10', 'EDF.dll+500010', f'EDF.dll+{perf_report.SYSTEM_UPDATE + 0x700:x}']] * 30
        check(perf_report.thread_role(img, stacks).startswith('game thread'), 'the game thread is told by System::update')
        lines = perf_report.phases(img, stacks, 10)
        check(any('object_update' in x for x in lines) and any('GiantAnt#5' in x for x in lines),
              'game thread samples land in their phase with the class below')
        # a plugin hook on a vtable slot, called by the ai_update loop: the plugin's time, by its outermost frame
        # (unsymbolized here: no DLL at hand, so the frame's own RVA)
        ai = f'EDF.dll+{perf_report.SYSTEM_UPDATE + 0x400:x}'
        hooked = ([['ntdll.dll+20', 'EDF6VehicleCrew.dll+2000', 'EDF6VehicleCrew.dll+1000', ai, 'EDF6.exe+10']] * 6 +
                  [['EDF.dll+500010', 'EDF6VehicleCrew.dll+1000', ai]] * 3 + [['ntdll.dll+20', ai]])
        lines = perf_report.phases(img, hooked, 10)
        check(any('ai_update' in x for x in lines) and any(x.strip() == '-  90.0% [plugin] EDF6VehicleCrew.dll+1000'
                                                           for x in lines),
              'a plugin hook below a phase is labelled by its outermost frame, also when it calls back into EDF.dll')
        check(any(x.strip() == '-  10.0% (in the phase itself / system code)' for x in lines),
              'only system code below the phase stays the phase\'s own')
        check(not perf_report.is_plugin('EDF6.exe+10') and perf_report.is_plugin('EDF6VehicleCrew.dll+1'),
              'EDF6.exe is not a plugin')
        fl = perf_report.plugin_functions(perf_report.Plugins(None), hooked, 10, 20.0)
        check('-  90.0%  18.00 ms/frame EDF6VehicleCrew.dll+1000 (self 0.0%)' in fl and
              '-  60.0%  12.00 ms/frame EDF6VehicleCrew.dll+2000 (self 60.0%)' in fl,
              'plugin functions: inclusive and self shares, ms of a frame; EDF.dll work they call is not their self')
        check(perf_report.Plugins(os.path.join(d, 'gone.dll')).dll is None, 'a DLL no longer there is not resolved')
        check(perf_report.short_name("crew::`anonymous namespace'::ThinkHook<0>") == 'crew::ThinkHook<0>',
              'PDB names lose the anonymous namespace')
        long_ = 'x' * 85
        check(perf_report.short_name(long_ + '::operator<') == long_ + '::operator<' and
              perf_report.short_name(f'std::unique_ptr<{long_}>::operator->') == 'std::unique_ptr<...>::operator->' and
              perf_report.short_name(f'f<{long_}>::g<int>') == 'f<...>' and
              perf_report.short_name(f'{long_}::operator>><int>') == f'{long_}::operator>><...>',
              "a long name's template arguments fold; an operator's own < > are not brackets")
        src = os.path.join(d, 'old.dll')   # --dll old.dll: perfrun installs it as EDF6VehicleCrew.dll
        with open(src, 'wb') as f:
            f.write(b'MZ')
        check(perf_report.Plugins(src, 'not its sha').dll is None and
              perf_report.Plugins(src, perf_report.file_sha256(src)).dll == src,
              'the DLL is resolved only when it is the file the run placed (sha256)')
        opened = []
        real_image = perf_report.Image
        perf_report.Image = lambda path, classes=True, names=True: opened.append(path) or None
        try:
            plug = perf_report.Plugins(src)
            plug.func('EDF6VehicleCrew.dll+1000', True)
            plug.func('old.dll+1000', True)
            plug.func('EDF6AutoTurret.dll+1000', True)
        finally:
            perf_report.Image = real_image
        check(opened == [src], "a renamed --dll is resolved under perfrun's install name, other modules are not")
        run = os.path.join(d, 'run')
        os.makedirs(run)
        with open(os.path.join(run, 'run.json'), 'w') as f:
            json.dump({'scenario': 'x', 'variant': 'A', 'windows': {'cpu': [t0 + 10, t0 + 20]}}, f)
        with open(os.path.join(run, 'vehiclecrew.log'), 'w') as f:
            f.write('[13:00:15.000] PERF 300 frames: mean 16.7 ms (60 fps), worst 30.0 ms, 6 over 20 ms, 0 over 33 ms\n')
        with open(os.path.join(run, 'cpu.txt'), 'w') as f:
            f.write('C 1 5000\nC total 5000\n')
        failed = os.path.join(d, 'failed')
        os.makedirs(failed)
        with open(os.path.join(failed, 'run.json'), 'w') as f:
            json.dump({'scenario': 'x', 'variant': 'B'}, f)
        t = perf_report.ab_table([run, failed])
        check('| A | run | 16.70 | n/a (counted steps) | 2.00% | 0 | 30.0 | 50 |' in t, 'the A/B table row (an older build: no steps a frame)')
        check('| AutoTurret |' in t, "the A/B table shows each round's AutoTurret")
        check('| B | failed | not sampled |' in t, 'a round without samples is a row, not a crash')


def c_format(path: str, head: str) -> str:
    """The format string of the plugin's Log("<head>...", ...) call in `path` (its adjacent literals joined), as a
    Python % format: the rows the parsers read are made from the plugin's own text, so the two cannot drift apart."""
    import re
    with open(os.path.join(ROOT, path), encoding='utf-8') as f:
        src = f.read()
    j = src.index('Log("' + head) + 4
    parts = []
    while True:
        k = j + 1
        buf = ''
        while src[k] != '"':
            buf += src[k:k + 2] if src[k] == '\\' else src[k]
            k += 2 if src[k] == '\\' else 1
        parts.append(buf)
        rest = src[k + 1:].lstrip()
        if not rest.startswith('"'):
            break
        j = len(src) - len(rest)
    fmt = ''.join(parts).replace('\\"', '"').replace("\\'", "'")
    return re.sub(r'%(?:ll|l)?([udXs])', lambda m: '%' + ('d' if m[1] in 'ud' else m[1]), fmt)


def probe_parsing() -> None:
    """The measurement probes' rows (src/perf_probe.cpp, crew.cpp PerfTick) and their parsers (cpu-1, hitch-1, hitch-5,
    gpu-2, latency-2, net-1, cpu-11), the rows made from the plugin's own format strings."""
    with tempfile.TemporaryDirectory() as d:
        t0 = time.mktime((2026, 11, 7, 13, 0, 0, 0, 0, -1))
        w = (t0 + 10, t0 + 20)
        log = os.path.join(d, 'vehiclecrew.log')
        perf = c_format('src/crew.cpp', 'PERF %u frames, %u steps')
        frame = c_format('src/perf_probe.cpp', 'PERF FRAME')
        render = c_format('src/perf_probe.cpp', 'PERF RENDER %u presents')
        net = c_format('src/perf_probe.cpp', 'PERF NET CHANNELS')
        gpu = c_format('src/perf_probe.cpp', 'PERF GPU a frame')
        slow = c_format('src/perf_probe.cpp', 'SLOW FRAME %lld %lld %.1f ms, %u steps: app')
        slow_bare = 'SLOW FRAME %d %d %.1f ms, %d steps'
        with open(log, 'w', encoding='utf-8') as f:
            f.write('[13:00:15.000] ' + perf % (300, 600, 16.7, 60, 30.0, 6, 20, 1, 33, 8.3, 120, 12.0) + '\n')
            f.write('[13:00:20.000] ' + perf % (100, 300, 50.0, 20, 80.0, 100, 20, 40, 33, 16.6, 60, 30.0) + '\n')
            f.write('[13:00:15.000] ' + frame % (300, 10, 0.1, 1.0, 4.0, 0.5, 8.0, 4.0, 0.5, 0.01, 3.0, 5.6) + '\n')
            f.write('[13:00:20.000] ' + frame % (100, 0, 0.1, 1.0, 6.0, 0.7, 40.0, 13.3, 1.5, 0.01, 1.0, 8.6) + '\n')
            f.write('[13:00:20.000] ' + render % (400, 0.2, 3.0, 12.5, 40.0, 20.0, 45.0, 90.0, 2,
                                                  1200, 300, 0, 0, 0, 7, 2500) + '\n')
            f.write('[13:00:15.000] ' + gpu % (40.0, 12.0, 0.0, 0.25, 0.0, 1.5, 300.0, 0.8) + '\n')
            f.write('[13:00:20.000] ' + gpu % (60.0, 8.0, 1.0, 0.35, 0.1, 2.5, 500.0, 1.2) + '\n')
            f.write('[13:00:20.000] ' + net % ('bit 0x4 10/500 B, bit 0x10 5/100 B', 'head 0x00000F01 3/90 B',
                                               20, 4.0, 0.2) + '\n')
            f.write('[13:00:16.000] ' + slow % (1000, 2000, 40.0, 3, 1.0, 5.0, 30.0, 2.0, 2.0) + '\n')
            f.write('[13:00:18.000] ' + slow_bare % (5000, 5500, 35.0, 2) + '\n')
            f.write('[13:00:40.000] ' + perf % (300, 300, 16.7, 60, 99.0, 9, 20, 9, 33, 16.7, 60, 99.0) + '\n')
        rows = perf_report.read_perf(log, w)
        check([(r[1], r[6]) for r in rows] == [(300, 600), (100, 300)], 'PERF frames and steps from the frame anchor')
        s = perf_report.frame_summary(rows)
        check(s['steps'] == 900 and abs(s['steps_per_frame'] - 2.25) < 1e-9 and s['unit'] == 'frame',
              'the frame summary counts frames, the steps a frame apart (cpu-1)')
        sp = perf_report.read_frame_split(log, w)
        check(sp['frames'] == 400 and sp['no step'] == 10 and abs(sp['draw'] - (300 * 4 + 100 * 6) / 400) < 1e-9,
              'PERF FRAME: each part weighted by the frames split')
        check(abs(sp['a step'] - (300 * 8 + 100 * 40) / (300 * 8 / 4 + 100 * 40 / 13.3)) < 1e-6,
              "a step: the steps' time over their count, not a mean of the rows' means")
        check(abs(sp['without steps'] - (300 * 5.6 + 100 * 8.6) / 400) < 1e-9, 'the n = 0 floor (cpu-11) read')
        r = perf_report.read_render(log, w)
        check(r['presents'] == 400 and r['stepped_worst'] == 90.0 and r['unpaired'] == 2 and r['submits'][0] == 1200
              and r['meshes'] == 2500, 'PERF RENDER: presents, input to present, submits by pass (gpu-2, latency-2)')
        g = perf_report.read_gpu(log, w)
        check(g['rows'] == 2 and abs(g['queries'] - 50.0) < 1e-9 and abs(g['pending'] - 10.0) < 1e-9
              and g['query_worst'] == 2.5 and abs(g['palettes'] - 400.0) < 1e-9 and abs(g['palette_ms'] - 1.0) < 1e-9,
              'PERF GPU: occlusion query reads and skin palettes a frame (gpu-5, gpu-6)')
        n = perf_report.read_net(log, w, 10.0)
        check(n['channels'][('out', 'bit 0x4')] == (1.0, 50.0) and n['channels'][('in', 'head 0x00000F01')] == (0.3, 9.0)
              and n['walks'] == 20, 'PERF NET CHANNELS: messages and bytes a second by channel, world walks (net-1)')
        sl = perf_report.read_slow(log)
        check(sl == [(1000, 2000, 40.0, 3, (1.0, 5.0, 30.0, 2.0, 2.0)), (5000, 5500, 35.0, 2, None)],
              'SLOW FRAME rows with and without the split (hitch-1)')
        st = os.path.join(d, 'stacks.txt')
        with open(st, 'w', encoding='utf-8') as f:
            f.write('D EDF.dll 1 2\nQ 10000000\nT 7 1 @1500 EDF.dll+10 EDF.dll+20\nT 7 1 @3000 EDF.dll+30\n'
                    'T 7 1 @5200 EDF.dll+40\nT 8 1 EDF.dll+50\n')
        ts = perf_report.read_timed_samples(st)
        check(ts['7'][0] == (1500, ['EDF.dll+10', 'EDF.dll+20']) and ts['8'] == [(None, ['EDF.dll+50'])],
              "rip_sampler's T lines with their QPC, and an older sampler's without")
        check(perf_report.in_slow(ts['7'], sl) == [['EDF.dll+10', 'EDF.dll+20'], ['EDF.dll+40']],
              'the stacks inside a slow frame by QPC')
        coop = os.path.join(d, 'coop.log')
        with open(coop, 'w', encoding='utf-8') as f:
            f.write('[2026-11-07 13:00:15.000] STATS last 5s: game sends 40 kbps avg, busiest second 80 kbps, to 2 '
                    'players, 90 B/packet avg (largest 900), 0% copies | wire up 50 kbps down 30 kbps, relayed 0\n')
        check(perf_report.read_coop(coop, w) == [(40.0, 80.0, 2, 50.0, 30.0)], "EDF6Coop's STATS rows (its dated stamp)")
        loads = os.path.join(d, 'loads.log')
        with open(loads, 'w', encoding='utf-8') as f:
            # src/mission_watch.cpp: each row names the phase starting and the one that ended with its ms
            phase = c_format('src/mission_watch.cpp', 'MISSION phase %s (%s')
            done = c_format('src/mission_watch.cpp', 'MISSION phases done')
            mem = c_format('src/bigworld.cpp', 'MEMORY %s')
            for h, ms in ((13, 3000), (14, 900)):
                f.write(f'[{h}:00:00.000] ' + phase % ("the game's player preload", 'since none', 0) + '\n'
                        f'[{h}:00:01.000] ' + phase % ('models', "the game's player preload", ms) + '\n'
                        f'[{h}:00:05.000] ' + done % ('models', 4000) + '\n'
                        f'[{h}:00:07.500] ' + mem % ('the map in', 5000, 6000, 4000, 9000, 32000, 20000, 40000) + '\n')
        ls = perf_report.read_loads(loads)
        check(len(ls) == 2 and ls[0]['preload_s'] == 5.0 and ls[0]['map_s'] == 2.5 and
              ls[0]['memory']['the map in'] == (5000, 6000, 4000) and
              ("the game's player preload", 900) in ls[1]['phases'],
              'MISSION phases and MEMORY points by entry (hitch-5)')
        txt = '\n'.join(perf_report.loads_text(ls))
        check('| 1 | cold (the launch first) |' in txt and '| 2 | warm (again) |' in txt, 'the first entry cold, the next warm')
        pm = os.path.join(d, 'presentmon.csv')
        with open(pm, 'w', encoding='utf-8') as f:
            f.write('Application,PresentMode,Dropped,MsBetweenDisplayChange,MsUntilDisplayed\n'
                    'EDF6.exe,Hardware: Independent Flip,0,16.6,20\nEDF6.exe,Hardware: Independent Flip,1,0,0\n'
                    'EDF6.exe,Hardware: Independent Flip,0,33.4,30\n')
        p = perf_report.presentmon_summary(pm)
        check(p['presents'] == 3 and p['dropped'] == 1 and abs(p['change_mean'] - 25.0) < 1e-9 and
              p['modes'] == {'Hardware: Independent Flip': 3}, 'PresentMon: display changes, not displayed (latency-2)')
        img = perf_report.Image.__new__(perf_report.Image)
        img.names = {}
        img.pdata = sorted([(0x111B8E0, 0x111B9E0), (perf_report.APP_FRAME, perf_report.APP_FRAME + 0x400),
                            (perf_report.SYSTEM_UPDATE, perf_report.SYSTEM_UPDATE + 0x1000), (0x118DFF0, 0x118E100),
                            (0x65F10, 0x66000)])
        img.starts = [b for b, _ in img.pdata]
        img.slot_class = {}
        check(perf_report.thread_role(img, [['ntdll.dll+1', 'EDF.dll+111b900', 'EDF.dll+3e700']] * 5) == 'render thread',
              'the render thread told by its entry, not by the xgs thread start every xgs thread has')
        check(perf_report.thread_role(img, [['ntdll.dll+1', 'EDF.dll+11a2eca', 'EDF.dll+3e700']] * 5) ==
              'game thread (main loop)', 'the game thread at full speed: in the main loop, not System::update')

        class Named:
            def func(self, f: str, leaf: bool) -> tuple[str, str]:
                return f, {'EDF6VehicleCrew.dll+10': 'crew::perfprobe::FenceHook'}.get(f, 'crew::Other')
        pf = perf_report.plugin_functions(Named(), [['ntdll.dll+1', 'EDF.dll+50e00', 'EDF6VehicleCrew.dll+10',
                                                     'EDF.dll+11a305f']] * 3, 10, 16.7)
        check(pf == ['no plugin frames'], 'the measurement probes are not the plugin\'s cost (they call through)')

        class Hooks:
            def func(self, f: str, leaf: bool) -> tuple[str, str]:
                return f, {'EDF6VehicleCrew.dll+20': 'crew::ThinkHook<0>', 'EDF6VehicleCrew.dll+30': 'crew::Think',
                           'EDF6VehicleCrew.dll+10': 'crew::perfprobe::UpdateHook'}.get(f, 'crew::Other')
        stock = ['EDF.dll+596b60', 'EDF6VehicleCrew.dll+20', 'EDF6VehicleCrew.dll+10', 'EDF.dll+1198000']
        own = ['ntdll.dll+1', 'EDF6VehicleCrew.dll+30', 'EDF6VehicleCrew.dll+20', 'EDF6VehicleCrew.dll+10']
        called = ['EDF.dll+7000', 'EDF6VehicleCrew.dll+30', 'EDF6VehicleCrew.dll+20', 'EDF.dll+1198000']
        check(not perf_report.plugin_owns(Hooks(), stock), "the stock Think under ThinkHook is the game's own work")
        check(perf_report.plugin_owns(Hooks(), own) and perf_report.plugin_owns(Hooks(), called),
              "the plugin's code, and the game code it calls, are its own work")
        pf = perf_report.plugin_functions(Hooks(), [stock, own, called, called], 10, 20.0)
        check(pf[0].endswith('100.0%, 20.00 ms/frame') and pf[1].endswith('75.0%, 15.00 ms/frame'),
              'both shares reported: on the stack, and its own work')
        def ph(*frames: str) -> str:
            return perf_report.phase_of(img, list(frames))[0]
        check(ph('EDF.dll+118e000', 'EDF.dll+1198fa0') == 'draw: camera / Umbra 0x118DFF0', 'the draw split by its part')
        check(ph('EDF.dll+65f20', 'EDF.dll+1199100') == 'ai_update (waiting on the xgs pool 0x65F10)',
              "a phase's wait on the xgs pool apart")
        check(ph('ntdll.dll+1', 'EDF.dll+705627') == 'app: network / Steam / EOS', 'Application::Update by call')
        check(ph('ntdll.dll+1', 'EDF.dll+11a305f') == 'main loop: render fence wait 0x111CC20', 'the main loop by part')
        check(ph('ntdll.dll+1') == 'outside the frame loop', 'nothing of the frame: outside it')


def vanilla_names() -> None:
    """Stack frames named from the vanilla indexes: edf6-vanilla-re's reviewed names over the code index's labels, only
    for the EDF.dll build they were made from; a source missing (CI) or of another build gives no names, no error."""
    sha = 'ab' * 32
    with tempfile.TemporaryDirectory() as d:
        code, re_ = os.path.join(d, 'code'), os.path.join(d, 're')
        os.makedirs(os.path.join(code, 'function-index'))
        os.makedirs(os.path.join(re_, 'shared', 'functions'))
        with open(os.path.join(code, 'function-index', 'summary.json'), 'w', encoding='utf-8') as f:
            json.dump({'images_sha256': {'EDF.dll': sha.upper()}}, f)
        with open(os.path.join(code, 'function-index', 'functions.tsv'), 'w', encoding='utf-8', newline='') as f:
            f.write('module\trva\tlabel\tlabel_kind\n'
                    'EDF.dll\t0x6A8B50\tFUN_1806a8b50_guess\tghidra\n'
                    'EDF.dll\t0x111B8E0\tTalkScript::Run\tsymbol\n'
                    'EDF.dll\t0x200000\tLAB_180200000\taddress_label\n'
                    'EDF6.exe\t0x1000\tWinMain\tsymbol\n')
        with open(os.path.join(re_, 'shared', 'builds.tsv'), 'w', encoding='utf-8', newline='') as f:
            f.write(f'build\tmodule\tsha256\nsteam\tEDF.dll\t{sha}\nother\tEDF.dll\t{"cd" * 32}\n')
        with open(os.path.join(re_, 'shared', 'functions', 'EDF.dll.tsv'), 'w', encoding='utf-8', newline='') as f:
            f.write('build\trva\tname\nsteam\t0x006A8B50\tWeapon_RadioContact::UpdateCall\n'
                    'other\t0x00300000\tNotThisBuild\n')
        names = perf_report.vanilla_names(sha, code, re_)
        check(names == {0x6A8B50: 'Weapon_RadioContact::UpdateCall', 0x111B8E0: 'TalkScript::Run'},
              f'vanilla names: reviewed over index, this build, no address labels or other modules ({names})')
        check(perf_report.vanilla_names('ef' * 32, code, re_) == {}, 'vanilla names: another build gives none')
        check(perf_report.vanilla_names(sha, os.path.join(d, 'none'), os.path.join(d, 'none')) == {},
              'vanilla names: missing sources give none')
        img = perf_report.Image.__new__(perf_report.Image)
        img.pdata, img.starts, img.slot_class, img.names = [(0x6A8B50, 0x6A8C00)], [0x6A8B50], {}, names
        check(img.name(0x6A8B50) == '0x6a8b50 Weapon_RadioContact::UpdateCall', f'named frame ({img.name(0x6A8B50)})')


def perfrun_probe_options() -> None:
    _, o = perfrun.options(['M001', '--dll', 'installed', '--cycle', '3', '--cycle-stay', '10'])
    check(o['cycle'] == 3 and o['cycle_stay'] == 10.0, '--cycle N and --cycle-stay S')
    _, o = perfrun.options(['M001', '--dll', 'installed', '--key', '4d@50', '--key', 'esc@60:500'])
    check(o['keys'] == [('4d', 50.0, 300), ('esc', 60.0, 500)], '--key SPEC@S[:MS], 300 ms by default')
    for bad in (['M001', '--dll', 'installed', '--presentmon', os.path.join(ROOT, 'no-such-presentmon.exe')],
                ['M001', '--dll', 'installed', '--cycle', '2', '--ab', __file__, __file__]):
        try:
            perfrun.options(bad)
            check(False, f'{bad[3]} refused')
        except SystemExit:
            pass
    a = perfrun.presentmon_args('pm.exe', 42, 20, 'o.csv')
    check(a[:3] == ['pm.exe', '--process_id', '42'] and '--v1_metrics' in a and '--terminate_after_timed' in a,
          'PresentMon: the game only, timed to the clean window, v1 columns')
    src = inspect.getsource(perfrun.one_run)
    check('cycle=(cycle, stay, False) if cycle else None' in src and 'during=None if cycle' in src,
          'a load run hands drive.run the cycle and samples nothing')
    check("coop.log" in src and 'run_log(clog, clog_at)' in src, "this launch's part of EDF6Coop.log kept as coop.log")
    with tempfile.TemporaryDirectory() as d:
        with open(os.path.join(d, 'run.json'), 'w') as f:
            json.dump({'scenario': 'M001', 'cycle': 2}, f)
        with open(os.path.join(d, 'vehiclecrew.log'), 'w') as f:
            f.write("[13:00:00.000] MISSION phase the game's player preload (in 0 ms)\n"
                    '[13:00:05.000] MISSION phases done (all 4000 ms)\n')
        t = perfrun.loads_report(d)
        check('left and entered again 2 times' in t and '| 1 | cold' in t and
              os.path.exists(os.path.join(d, 'loads.md')), "a load run's loads.md")


def drive_during() -> None:
    import drive
    src = inspect.getsource(drive._session)   # drive.run's launch and stay
    check('during' in inspect.signature(drive.run).parameters and 'not worker.is_alive()' in src,
          "drive.run's stay ends when `during` returns")


def ini_option() -> None:
    _, o = perfrun.options(['stress:1', '--ini', 'HavokMultithread=1', '--ini', 'Debug=0', '--dll', 'installed'])
    check(o['ini'] == ['HavokMultithread=1', 'Debug=0'], '--ini repeats, in order')
    check("ini=('Debug=1', 'PerfProbe=1', *opts['ini'])" in inspect.getsource(perfrun.one_run),
          '--ini reaches drive.run after Debug=1 and PerfProbe=1')
    try:
        perfrun.options(['stress:1', '--ini', 'HavokMultithread'])
        check(False, '--ini without = refused')
    except SystemExit:
        pass


def log_turnover() -> None:
    import shutil
    import tempfile
    d = tempfile.mkdtemp()
    try:
        log = os.path.join(d, 'EDF6VehicleCrew.log')
        with open(log, 'wb') as f:
            f.write(b'old launch\r\n')
        mark = perfrun.log_mark(log)
        with open(log, 'ab') as f:
            f.write(b'this launch\r\n')
        check(perfrun.run_log(log, mark) == b'this launch\r\n', 'no turnover: from the mark on')
        with open(log, 'ab') as f:
            f.write(b'before the rename\r\n')
        os.replace(log, log + '.1')
        with open(log, 'wb') as f:
            f.write(b'(log continued)\r\nafter\r\n')
        check(perfrun.run_log(log, mark) == b'this launch\r\nbefore the rename\r\n(log continued)\r\nafter\r\n',
              'one turnover: .log.1 from the mark, then the new log')
        os.replace(log, log + '.1')
        with open(log, 'wb') as f:
            f.write(b'third\r\n')
        got = perfrun.run_log(log, mark)
        check(got.startswith(b'(perfrun: the log turned over more than once') and got.endswith(b'third\r\n'),
              'two turnovers: said so, what is left kept')
        check(perfrun.run_log(os.path.join(d, 'none.log'), (0, 0)) is None, 'no log: nothing')
    finally:
        shutil.rmtree(d)


def both_plugins() -> None:
    import shutil
    import tempfile
    d = tempfile.mkdtemp()
    try:
        crew = os.path.join(d, 'EDF6VehicleCrew.dll')
        open(crew, 'wb').close()
        check(perfrun.placements(crew) == (f'{crew}=Plugins/EDF6VehicleCrew.dll',), 'no AutoTurret built: the crew alone')
        turret = os.path.join(d, 'EDF6AutoTurret.dll')
        open(turret, 'wb').close()
        check(perfrun.placements(crew) == (f'{crew}=Plugins/EDF6VehicleCrew.dll', f'{turret}=Plugins/EDF6AutoTurret.dll'),
              "the same build's AutoTurret placed with it")
        check(perfrun.placements(None) == (), '--dll installed: nothing placed')
        # what the run records is what the game loads, taken before the launch
        game = os.path.join(d, 'game')
        gplugins = os.path.join(game, 'Mods', 'Plugins')
        os.makedirs(gplugins)
        with open(turret, 'wb') as f:
            f.write(b'built turret')
        got = perfrun.loaded_plugins(game, crew)
        check((got['turret'], got['turret_source'], got['turret_sha256']) ==
              (turret, 'built-alongside', perfrun.sha256(turret)), "AutoTurret built alongside: that build's recorded")
        lone = os.path.join(d, 'lone')
        os.makedirs(lone)
        lone_crew = os.path.join(lone, 'EDF6VehicleCrew.dll')
        open(lone_crew, 'wb').close()
        got = perfrun.loaded_plugins(game, lone_crew)
        check((got['turret'], got['turret_source'], got['turret_sha256']) == (None, 'none', None),
              'no AutoTurret anywhere: recorded as none, not guessed')
        game_turret = os.path.join(gplugins, 'EDF6AutoTurret.dll')
        with open(game_turret, 'wb') as f:
            f.write(b'old installed turret')
        got = perfrun.loaded_plugins(game, lone_crew)
        check((got['turret'], got['turret_source'], got['turret_sha256']) ==
              (game_turret, 'game-folder', perfrun.sha256(game_turret)),
              "no AutoTurret next to --dll: the game folder's, the one the game loads, recorded")
        with open(os.path.join(gplugins, 'EDF6VehicleCrew.dll'), 'wb') as f:
            f.write(b'installed crew')
        got = perfrun.loaded_plugins(game, None)
        check(got['turret_source'] == 'game-folder' and got['sha256'] == perfrun.sha256(
              os.path.join(gplugins, 'EDF6VehicleCrew.dll')), "--dll installed: the game folder's both")
        check(perfrun.ab_turrets(game, crew, lone_crew) is not None and
              perfrun.ab_turrets(game, lone_crew, crew) is not None,
              "--ab with the build's AutoTurret on one side only: refused")
        check(perfrun.ab_turrets(game, crew, crew) is None and perfrun.ab_turrets(game, lone_crew, lone_crew) is None,
              '--ab with AutoTurret alongside both or neither: runs')
    finally:
        shutil.rmtree(d)


def outlived_runs() -> None:
    """The game outliving a run (quit_wait passed): one_run says what was not put back for a stock mission too, and
    --restore puts back both the range mission and what drive.py placed (drive.restore_placed), under the lease."""
    import drive
    saved = (drive.game_pids, drive.run, drive.restore_placed, gen.restore_scenario, perfrun.gamedir.find_or_dev)
    calls = []
    d = tempfile.mkdtemp()
    try:
        game = os.path.join(d, 'game')
        os.makedirs(os.path.join(game, 'Mods', 'Plugins'))
        os.environ['EDF6_QUEUE_DIR'] = os.path.join(d, 'queues')
        running = []
        drive.game_pids = lambda: list(running)

        def fake_run(*a, **k):
            os.makedirs(drive.placed_dir(game), exist_ok=True)   # what it placed (the ini at least) is recorded
            running.append(1)   # the game does not leave
            return 1
        drive.run = fake_run
        import contextlib
        import io
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = perfrun.one_run(game, 'M001', os.path.join(d, 'out'), None,
                                 dict(warm=1.0, cpu=1, stacks=0, interval=2, difficulty=1, ini=[], shots=False))
        check(rc != 0 and 'perfrun.py --restore' in out.getvalue() and 'placed plugins' in out.getvalue(),
              'a stock mission the game outlived: not put back, said so, with the one command that does')
        with open(os.path.join(d, 'out', 'run.json'), encoding='utf-8') as f:
            run = json.load(f)
        check('turret_source' in run and 'sha256' in run, 'run.json records the plugins taken before the launch')
        running.clear()
        drive.restore_placed = lambda g: calls.append(('placed', g)) or True
        gen.restore_scenario = lambda g: calls.append(('range', g)) or False
        perfrun.gamedir.find_or_dev = lambda: game
        with contextlib.redirect_stdout(io.StringIO()):
            rc = perfrun.main(['--restore'])
        check(rc == 0 and calls == [('range', game), ('placed', game)],
              '--restore puts back the range mission and what drive.py placed')
        calls.clear()
        drive.restore_placed = lambda g: calls.append(('placed', g)) and False
        with contextlib.redirect_stdout(io.StringIO()):
            rc = perfrun.main(['--restore'])
        check(rc == 1, '--restore fails while a placed file is left (changed by someone else)')
        # The game leaving between drive.run's own look (still there: nothing put back) and one_run's: one_run puts
        # back what the disk says is left, and a file left over fails the run.
        calls.clear()
        drive.restore_placed = lambda g: calls.append(('placed', g)) or True

        def late_exit(*a, **k):
            return 1   # drive.run saw the game, left everything; it is gone now
        drive.run = late_exit
        with contextlib.redirect_stdout(io.StringIO()):
            rc = perfrun.one_run(game, 'M001', os.path.join(d, 'out2'), None,
                                 dict(warm=1.0, cpu=1, stacks=0, interval=2, difficulty=1, ini=[], shots=False))
        check(rc == 1 and calls == [('placed', game)], 'the game gone after drive.run: one_run puts back what is left')
        calls.clear()
        drive.restore_placed = lambda g: calls.append(('placed', g)) and False
        drive.run = lambda *a, **k: 0
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = perfrun.one_run(game, 'M001', os.path.join(d, 'out3'), None,
                                 dict(warm=1.0, cpu=1, stacks=0, interval=2, difficulty=1, ini=[], shots=False))
        check(rc == 1 and calls == [('placed', game)] and 'perfrun.py --restore' in out.getvalue(),
              'a placed file left over: the run fails, with the command that puts it back')
    finally:
        drive.game_pids, drive.run, drive.restore_placed, gen.restore_scenario, perfrun.gamedir.find_or_dev = saved
        import shutil
        shutil.rmtree(d, ignore_errors=True)


def rewrite_files() -> None:
    """gen.rewrite_scenario: refused before a write_scenario kept the originals; then the range's files rewritten with
    the kept originals left as they were, so restore_scenario puts back what was there before the first write."""
    with tempfile.TemporaryDirectory() as root, tempfile.TemporaryDirectory() as queues:
        os.environ['EDF6_QUEUE_DIR'] = queues
        out = gen.mission_dir(root, gen.RANGE_MISSION)
        os.makedirs(out)
        with open(os.path.join(out, 'MISSION.AC'), 'wb') as f:
            f.write(b'old script')
        saved = {k: getattr(gen, k) for k in ('installed', 'Game', '_range_files', 'without_missing',
                                              'has_mission_setup', '_write_mission')}
        scripts = iter(['first leg', 'second leg', 'third leg'])

        def fake_write(game_root, game, mission, site, text, points):
            for name, data in (('MISSION.AC', text.encode()), ('MISSION.RMPA', points)):
                with open(os.path.join(gen.mission_dir(game_root, mission), name), 'wb') as f:
                    f.write(data)
        gen.installed = lambda game_root: True
        gen.Game = lambda game_root: None
        gen.without_missing = lambda game_root, plan: (plan, ['left out'])
        gen.has_mission_setup = lambda game, name: True
        gen._range_files = lambda game_root, game, plan: (next(scripts), b'points', [], False)
        gen._write_mission = fake_write
        try:
            plan = gen.Plan()
            plan.vehicles = {}
            plan.waves.enabled = False
            try:
                gen.rewrite_scenario(root, plan)
                check(False, 'rewrite_scenario refused before write_scenario')
            except RuntimeError:
                with open(os.path.join(out, 'MISSION.AC'), 'rb') as f:
                    check(f.read() == b'old script', 'rewrite_scenario refused before write_scenario, nothing written')
            gen._range_files = lambda game_root, game, plan: (next(scripts), b'points', [], False)
            scripts = iter(['first leg', 'second leg', 'third leg'])
            gen.write_scenario(root, plan)
            check(gen.rewrite_scenario(root, plan) == ['left out'], 'rewrite_scenario returns what was left out')
            with open(os.path.join(out, 'MISSION.AC'), 'rb') as f:
                check(f.read() == b'second leg', 'rewrite_scenario writes the next leg\'s script')
            with open(os.path.join(gen.scenario_backup(root), 'MISSION.AC'), 'rb') as f:
                check(f.read() == b'old script', 'the kept original is still the one from before the first leg')
            gen.rewrite_scenario(root, plan)
            check(gen.restore_scenario(root), 'restore after two rewrites')
            with open(os.path.join(out, 'MISSION.AC'), 'rb') as f:
                check(f.read() == b'old script', 'restore puts back the script from before the first leg')
            check(not os.path.exists(os.path.join(out, 'MISSION.RMPA')), 'and removes what was not there before it')
            gen.installed = lambda game_root: False
            try:
                gen.check_scenario(root, plan)
                check(False, 'check_scenario refuses a range that is not installed')
            except RuntimeError:
                check(not os.path.exists(gen.scenario_backup(root)), 'check_scenario refuses, nothing kept')
        finally:
            for k, v in saved.items():
                setattr(gen, k, v)


def legs_options() -> None:
    """The positional arguments as legs: their options, the refusals, the folders, the warm-ups, the commands."""
    import drive
    scenario, o = perfrun.options(['stress:1', 'stress:10', 'M001,key=4d@48,key=esc@57:500,warm=30,diff=2', 'RM015',
                                   '--dll', 'installed'])
    legs = o['legs']
    check(scenario == 'stress:1' and [g['scenario'] for g in legs] == ['stress:1', 'stress:10', 'M001', 'RM015'],
          'several positional arguments: the legs in order')
    check(legs[2]['keys'] == [('4d', 48.0, 300), ('esc', 57.0, 500)] and legs[2]['warm'] == 30.0
          and legs[2]['difficulty'] == 2 and legs[0]['keys'] == [] and legs[0]['warm'] is None,
          "a leg's key= (300 ms by default) / warm= / diff=, the others' none")
    check(o['keys'] == [] and o['leg_timeout'] == 900.0 and not o['next_reboot'], "legs: the run's own keys untouched")
    scenario, o = perfrun.options(['M001,key=4d@48,warm=10,diff=3', '--dll', 'installed'])
    check(scenario == 'M001' and o['keys'] == [('4d', 48.0, 300)] and o['warm'] == 10.0 and o['difficulty'] == 3
          and len(o['legs']) == 1, 'one scenario with leg options: the single run as before, its options the run\'s')
    _, o = perfrun.options(['M001', 'RM015', '--dll', 'installed', '--leg-timeout', '300', '--next-reboot'])
    check(o['leg_timeout'] == 300.0 and o['next_reboot'], '--leg-timeout S, --next-reboot')
    for bad in (['M001', 'RM015', '--dll', 'installed', '--ab', __file__, __file__],
                ['M001', 'RM015', '--dll', 'installed', '--cycle', '2'],
                ['M001', 'RM015', '--dll', 'installed', '--key', '4d@50'],
                ['M001,foo=1', '--dll', 'installed'], ['M001,warm=x', '--dll', 'installed'],
                ['M001,diff=7', '--dll', 'installed'], [',key=4d@1', '--dll', 'installed'],
                ['M001,key', '--dll', 'installed'], ['M001,key=4d', '--dll', 'installed'],
                ['M001,warm=-1', '--dll', 'installed'], ['M001,warm=nan', '--dll', 'installed'],
                ['M001,warm=inf', '--dll', 'installed'], ['M001,key=4d@nan', '--dll', 'installed'],
                ['M001,key=4d@-2', '--dll', 'installed'], ['M001,key=4d@1:0', '--dll', 'installed'],
                ['M001', '--warm', 'nan', '--dll', 'installed'], ['M001', '--warm', '-5', '--dll', 'installed'],
                ['M001', 'RM015', '--leg-timeout', '0', '--dll', 'installed'],
                ['M001', 'RM015', '--leg-timeout', 'inf', '--dll', 'installed'],
                ['M001', '--cpu', '0', '--dll', 'installed'], ['M001', '--difficulty', '9', '--dll', 'installed'],
                ['M001', '--stacks', '-1', '--dll', 'installed'],
                ['M001', '--restore'], ['--dll', 'installed']):
        try:
            perfrun.options(bad)
            check(False, f'{bad} refused')
        except SystemExit:
            check(True, f'{" ".join(bad)} refused')
    check(perfrun.leg_names([{'scenario': x} for x in ('stress:1', 'M001', 'stress:1', 'stress:1')])
          == ['stress_1', 'M001', 'stress_1-2', 'stress_1-3'], "a leg's folder: its scenario, -2 on one run twice")
    check(perfrun.warm_for('stress:1', None) == perfrun.stress_fields(1)[2] and perfrun.warm_for('stress:100', None)
          == perfrun.stress_fields(100)[2] == 90.0 and perfrun.warm_for('M001', None) == 25.0
          and perfrun.warm_for('grand', None) == 45.0 and perfrun.warm_for('stress:100', 12.0) == 12.0,
          "each leg's warm-up: its scenario's own, or the one given")
    check([perfrun.leg_mission(x) for x in ('stress:10', 'swarm:giantspider01', 'blast', 'grand', 'M001', 'RM015', '42')]
          == ['range'] * 4 + ['M001', 'RM015', '42'], "a leg's mission: range for a range scenario")
    check(drive.next_command('range', 1) == 'next range 1' and drive.next_command('M001', 2, True)
          == 'next M001 2 reboot', "a leg's `next` command")
    check(perfrun.legs_longest([40.0, 25.0], 20, 20, 900) == int(40 + 40 + 25 + 40 + 2 * 900 + 90),
          "the launch's deadline: every leg's sampling and every leg's way in (the first one's loading too)")
    check(perfrun.options(['M001,warm=0', '--dll', 'installed'])[1]['warm'] == 0.0, 'warm=0: sampled at once')


def legs_logs() -> None:
    """The autopilot's NEXT rows, and the launch's logs cut a leg each by the wall clock (past midnight too)."""
    import drive
    rows = ['[ 70.0 s] NEXT armed: M001, difficulty 1, back in by HQMain', '[ 71.0 s] CYCLE 1/1 leaving',
            '[ 90.0 s] NEXT 1 entered: the mission runs (row 1, difficulty 1, mode 0, content id 0)']
    check(drive.next_outcome(rows, 1) == 'entered' and drive.next_outcome(rows[:2], 1) is None,
          'NEXT k entered: the leg is in its mission; not yet before it')
    check(drive.next_outcome(['[ 90.0 s] NEXT 10 entered: ...'], 1) is None, 'NEXT 10 is not NEXT 1')
    check(drive.next_outcome(['[ 1.0 s] NEXT refused: a cycle or a next is already running'], 1) == 'failed'
          and drive.next_outcome(['[ 1.0 s] CYCLE failed: the script went back to the title'], 2) == 'failed',
          'NEXT refused / CYCLE failed: the leg failed')
    start = time.mktime((2026, 11, 24, 23, 59, 0, 0, 0, -1))
    data = (b'[23:58:59.900] stamped a little before the start\r\n'
            b'[23:59:10.000] PERF OBJECTS enemies 160, player team 4, friends 9\r\n'
            b'  a dump row without a stamp\r\n'
            b'[2026-11-25 00:00:40] STATS last 5s: the date first (EDF6Coop)\r\n'
            b'[00:00:30.000] PERF OBJECTS enemies 1600, player team 4, friends 9\r\n'
            b'[00:01:00.000] at the second bound\r\n')
    a, b, c = perfrun.split_log(data, start, [start + 60, start + 120])
    check(a.count(b'\r\n') == 3 and b'a little before' in a and b'without a stamp' in a and b'160,' in a,
          'the first leg: its rows, a stamp just before the start and a row without a stamp with it')
    check(b'1600' in b and b'date first' in b and b'160,' not in b, 'the second leg: past midnight, the date-first rows')
    check(c == b'[00:01:00.000] at the second bound\r\n', 'a row at a bound goes to the leg after it')
    check(perfrun.split_log(data, start, []) == [data], 'one leg: the whole log')
    # perf_report's windows over midnight: 23:59:00 + 60 s; rows before, inside (both sides of 00:00) and after
    d = tempfile.mkdtemp()
    try:
        log = os.path.join(d, 'vehiclecrew.log')
        with open(log, 'w', encoding='utf-8') as f:
            f.write('[23:58:30.000] PERF OBJECTS enemies 1, player team 4, friends 9\n'
                    '[23:59:30.000] PERF OBJECTS enemies 10, player team 4, friends 9\n'
                    '[00:00:00.000] PERF OBJECTS enemies 20, player team 4, friends 9\n'
                    '[00:00:30.000] PERF OBJECTS enemies 1000, player team 4, friends 9\n'
                    '[00:00:00.400] PERF 300 frames, 300 steps: mean 20.0 ms (50 fps), worst 30.0 ms, 3 over 20 ms, '
                    '0 over 33 ms\n')
        objs = perf_report.read_objects(log, (start, start + 60))
        check(objs.get('enemies') == (15.0, 20), f'PERF OBJECTS over midnight: the window\'s rows only ({objs})')
        check(len(perf_report.read_perf(log, (start, start + 60))) == 1, 'PERF rows over midnight kept')
        check(perf_report.in_window(perfrun.tod(start) - 60, (start, start + 60)) is False,
              'a row a minute before the window is before it, not a day later')
    finally:
        import shutil
        shutil.rmtree(d, ignore_errors=True)


def wave_ready() -> None:
    import drive
    from unittest.mock import patch

    with tempfile.TemporaryDirectory() as d:
        log = os.path.join(d, 'vehiclecrew.log')
        with open(log, 'wb') as f:
            f.write(b'[00:00:00.000] PERF OBJECTS enemies 30, player team 1, friends 19\n')
        during = iter((b'[00:00:10.000] PERF OBJECTS enemies 0, player team 1, friends 4\n',
                       b'[00:00:15.000] PERF OBJECTS enemies 10, player team 1, friends 19\n'))
        polls = []

        def advance(_):
            polls.append(1)
            with open(log, 'ab') as f:
                f.write(next(during))

        with patch.object(drive, 'game_pids', return_value=[1]), patch.object(perfrun.time, 'sleep', side_effect=advance):
            ready = perfrun.wait_population(d, log, 60.0)
        check(ready is None and len(polls) == 2, 'wave readiness ignores old counts and waits for a live enemy')
        with patch.object(drive, 'game_pids', return_value=[]):
            check('exited' in perfrun.wait_population(d, log, 60.0), 'game exit stops a wait for the wave')


def legs_run() -> None:
    """One launch of three legs with drive.run faked: the range written once before the launch and rewritten for the
    later range leg, the `next` commands, the keys timed from the leg's entry, each leg's slice of the log and its
    run.json, the range and the placed files put back once; summary.md's row a leg; a leg that never comes in."""
    import contextlib
    import io
    import drive
    names = ('game_pids', 'run', 'rows', 'command', 'key', 'restore_placed', 'anything_left')
    saved = {k: getattr(drive, k) for k in names}
    gsaved = {k: getattr(gen, k) for k in ('installed', 'check_scenario', 'write_scenario', 'rewrite_scenario',
                                           'restore_scenario')}
    sample_saved = perfrun._sample
    population_saved = perfrun.wait_population
    d = tempfile.mkdtemp()
    try:
        game = os.path.join(d, 'game')
        os.makedirs(os.path.join(game, 'Mods', 'Plugins'))
        os.environ['EDF6_QUEUE_DIR'] = os.path.join(d, 'queues')
        log = perfrun.plugin_log(game)
        with open(log, 'wb') as f:
            f.write(b'[00:00:00.000] an earlier launch\r\n')
        with open(os.path.join(game, 'Mods', 'Plugins', 'EDF6VehicleCrew.ini'), 'w', encoding='utf-8') as f:
            f.write('[VehicleCrew]\nTestRangeContent=4242\n')
        calls: list = []
        auto: list[str] = []
        running: list[int] = []
        enemies = {'range': 0}
        enter = ['ok']   # how a `next` goes: ok, never (no row), mode (the pack's mode not entered), wrong (difficulty)
        running_at = [0.0]   # when the first mission runs (its RUNNING row), its loading before
        targets = {'M001': (1, 0, 0), 'range': (0, 4242, 7)}   # row, content id, mode

        def stamp(text: str) -> None:
            t = int(perfrun.tod(time.time()) * 1000)   # once, truncated as the plugin's stamp: never past now
            with open(log, 'ab') as f:
                f.write(f'[{t // 3600000:02d}:{t // 60000 % 60:02d}:{t // 1000 % 60:02d}.{t % 1000:03d}] {text}\r\n'
                        .encode())

        def fake_run(game_, mission, difficulty, seconds, keep, **k):
            calls.append(('run', mission, difficulty, k.get('extra'), isinstance(k.get('gate'), drive.Gate)))
            running.append(1)
            stamp(f'the launch: {mission}')
            row, content, mode = targets[mission]
            running_at[0] = time.time() + 0.3
            first = [f'[ 0.5 s] RANGE mode {mode} (content id {content}) made current'] if content else []
            auto[:] = first
            runs = (f'[ 9.0 s] RUNNING: the mission runs (row {row}, difficulty {difficulty}, mode {mode}, '
                    f'content id {content})')
            rows_now[0] = lambda: (auto[:len(first)] + [runs] + auto[len(first):] if time.time() >= running_at[0]
                                   else list(auto))
            k['during']()
            running.clear()
            return 0

        def fake_command(game_, text, gate=None):
            calls.append(('command', text))
            time.sleep(0.01)
            stamp(f'left for {text}')
            _, mission, diff = text.split()[:3]
            row, content, mode = targets[mission]
            n = sum(1 for c in calls if c[0] == 'command')
            if enter[0] == 'mode':
                auto.append('[ 1.0 s] RANGE no offline mode with content id 4242')
                auto.append("[ 1.0 s] SCRIPT HQMain: the pack's mode not entered, the menu as is")
            elif enter[0] in ('ok', 'wrong'):
                auto.append(f'[ 1.0 s] RANGE mode {mode} (content id {content}) made current')
                d = int(diff) + (1 if enter[0] == 'wrong' else 0)
                auto.append(f'[ 1.0 s] NEXT {n} entered: the mission runs (row {row}, difficulty {d}, mode {mode}, '
                            f'content id {content})')

        def fake_sample(out, warm, cpu_s, stacks_s, interval, marks, presentmon=None):
            calls.append(('sample', os.path.basename(out), warm))
            now = time.time()
            n = 0 if enter[0] == 'ended' else enemies['range']
            stamp('PERF 300 frames, 300 steps: mean 20.0 ms (50 fps), worst 30.0 ms, 3 over 20 ms, 0 over 33 ms')
            stamp(f'PERF OBJECTS enemies {n}, player team 4, friends 9')
            with open(os.path.join(out, 'cpu.txt'), 'w') as f:
                f.write('')
            marks['pid'] = 1
            marks['windows'] = {'cpu': [now - 30, now + 0.2], 'stacks': [now + 0.2, now + 0.3]}

        def write(game_, plan):
            calls.append(('write', plan.waves.max_alive))
            enemies['range'] = plan.waves.max_alive
            return []

        def rewrite(game_, plan):
            calls.append(('rewrite', plan.waves.max_alive))
            enemies['range'] = plan.waves.max_alive
            return ['left out']
        drive.game_pids = lambda: list(running)
        drive.run = fake_run
        rows_now = [lambda: list(auto)]
        drive.rows = lambda game_: rows_now[0]()
        drive.command = fake_command
        drive.key = lambda game_, spec, ms, gate=None: calls.append(('key', spec, ms))
        drive.restore_placed = lambda game_: calls.append(('placed',)) or True
        drive.anything_left = lambda game_: False
        gen.installed = lambda game_: True
        gen.check_scenario = lambda game_, plan: calls.append(('check', plan.waves.max_alive)) or []
        gen.write_scenario = write
        gen.rewrite_scenario = rewrite
        gen.restore_scenario = lambda game_: calls.append(('restore',)) or True
        perfrun._sample = fake_sample

        def fake_population(game_, log_, timeout, gate):
            calls.append(('population',))
            return 'no live enemy wave' if enter[0] == 'empty' else None

        perfrun.wait_population = fake_population
        _, opts = perfrun.options(['stress:1', 'M001,key=4d@0.1,diff=3', 'stress:10,warm=7', '--dll', 'installed',
                                   '--warm', '5'])
        out = os.path.join(d, 'out')
        with contextlib.redirect_stdout(io.StringIO()):
            rc = perfrun.multi_run(game, opts['legs'], out, None, opts)
        check(rc == 0, 'three legs: the run passes')
        order = [c for c in calls if c[0] != 'key']
        check(order == [('check', 160), ('check', 1600), ('write', 160), ('run', 'range', 1, ('running',), True),
                        ('population',), ('sample', 'stress_1', 5.0), ('command', 'next M001 3'), ('sample', 'M001', 5.0),
                        ('rewrite', 1600), ('command', 'next range 1'), ('population',), ('sample', 'stress_10', 7.0),
                        ('restore',), ('placed',)],
              'every range leg checked before the launch, written once, rewritten for the later range leg, `next` '
              'for each later leg, put back once at the end')
        check(('key', '4d', 300) in calls, "the M001 leg's key held")
        dirs = [os.path.join(out, n) for n in ('stress_1', 'M001', 'stress_10')]
        runs = []
        for x in dirs:
            with open(os.path.join(x, 'run.json'), encoding='utf-8') as f:
                runs.append(json.load(f))
        check([r['scenario'] for r in runs] == ['stress:1', 'M001', 'stress:10'] and [r['leg'] for r in runs]
              == [1, 2, 3] and runs[1]['difficulty'] == 3 and runs[2]['skipped'] == ['left out']
              and all('cpu' in r['windows'] for r in runs) and 'asked' in runs[1] and 'asked' not in runs[0],
              "each leg's run.json: its scenario, difficulty, windows, when its `next` was handed")
        check(runs[1]['keys_held'][0][:3] == ['4d', 0.1, 300], "the leg's key held, timed from its entry")
        check(runs[0]['entered'] >= running_at[0],
              "the first leg clocks entry from the RUNNING row")
        check(runs[0]['population_ready'] >= runs[0]['entered'] and 'population_ready' not in runs[1],
              'only a wave leg waits for population before warm-up')
        with open(os.path.join(dirs[0], 'run.json'), encoding='utf-8') as f:
            first = json.load(f)
        with open(os.path.join(dirs[0], 'run.json'), 'w', encoding='utf-8') as f:
            json.dump(dict(first, error='rip_sampler (stacks) exit 5: denied'), f)
        row0 = perfrun.legs_table(dirs).splitlines()[2]
        check(row0.endswith('| **ERROR** rip_sampler (stacks) exit 5: denied |') and '| 20.00 |' in row0,
              "summary.md: a sampled leg with an error says so in its row")
        with open(os.path.join(dirs[0], 'run.json'), 'w', encoding='utf-8') as f:
            json.dump(first, f)
        slices = []
        for x in dirs:
            with open(os.path.join(x, 'vehiclecrew.log'), 'rb') as f:
                slices.append(f.read())
        check(b'the launch' in slices[0] and b'an earlier launch' not in slices[0]
              and b'left for next M001' in slices[1] and b'left for next range' in slices[2]
              and b'enemies 1600' in slices[2] and b'enemies 1600' not in slices[0] + slices[1],
              "each leg's part of this launch's log: its leaving, its loading, its rows")
        with open(os.path.join(out, 'vehiclecrew.log'), 'rb') as f:
            check(f.read() == b''.join(slices), "the whole launch's log kept beside the legs'")
        table = perfrun.legs_table(dirs)
        lines = table.splitlines()
        check(len(lines) == 5 and lines[2].startswith('| stress_1 | stress:1 | 5 | 20.00 | 50.0 | 1.00% | 1.00 |')
              and 'enemies 160 (max 160)' in lines[2] and 'enemies 1600 (max 1600)' in lines[4]
              and 'enemies 160 ' in lines[3], "summary.md: a row a leg, each leg's frames and objects")
        with open(os.path.join(dirs[0], 'report.md'), 'w', encoding='utf-8') as f:
            f.write("the plugin's own work (not the game's own under a hook that only calls through): 4.2%, 0.8 ms/frame\n")
        check('| 4.2% |' in perfrun.legs_table(dirs).splitlines()[2], "summary.md: the plugin's own work from report.md")
        calls.clear()
        enter[0] = 'empty'
        with contextlib.redirect_stdout(io.StringIO()):
            rc = perfrun.multi_run(game, opts['legs'], os.path.join(d, 'out-empty'), None, opts)
        check(rc == 1 and ('population',) in calls and not any(c[0] == 'sample' for c in calls)
              and ('restore',) in calls, 'an empty wave leg never samples and restores the range')
        calls.clear()
        enter[0] = 'ended'
        out = os.path.join(d, 'out-ended')
        with contextlib.redirect_stdout(io.StringIO()):
            rc = perfrun.multi_run(game, opts['legs'][:1], out, None, opts)
        with open(os.path.join(out, 'stress_1', 'run.json'), encoding='utf-8') as f:
            bad = json.load(f)
        check(rc == 1 and 'cpu' in bad['windows'] and 'no live enemies' in bad['error'] and ('restore',) in calls,
              'an initially populated battle that empties before measurement is not a valid sample')
        # a leg that never comes in: the run ends there, failed, everything still put back
        calls.clear()
        auto.clear()
        enter[0] = 'never'
        opts['leg_timeout'] = 2.0   # the first leg's RUNNING comes 0.3 s in: in time; the second leg's never
        out = os.path.join(d, 'out2')
        with contextlib.redirect_stdout(io.StringIO()) as said:
            rc = perfrun.multi_run(game, opts['legs'], out, None, opts)
        check(rc == 1 and ('restore',) in calls and ('placed',) in calls and not any(c[0] == 'rewrite' for c in calls)
              and 'not sampled' in said.getvalue(), 'a leg not in its mission in time: failed, the run ends, put back')
        with open(os.path.join(out, 'M001', 'run.json'), encoding='utf-8') as f:
            check('--leg-timeout' in json.load(f)['error'], "the leg's run.json says why")
        table = perfrun.legs_table([os.path.join(out, n) for n in ('stress_1', 'M001', 'stress_10')]).splitlines()
        check('not sampled: next 1 not in its mission' in table[3] and 'not sampled: not reached' in table[4]
              and table[3].endswith('| **ERROR** next 1 not in its mission 2 s after it was asked (--leg-timeout) |')
              and table[4].endswith('| **ERROR** not sampled |'), 'summary.md: why a leg has no numbers, marked')
        # the pack's mode not entered: the leg failed at once, not after --leg-timeout
        for how, want in (('mode', "failed: [ 1.0 s] SCRIPT HQMain: the pack's mode not entered"),
                          ('wrong', 'next 1 entered another mission than M001 (difficulty 3): difficulty 4, not 3')):
            calls.clear()
            enter[0] = how
            opts['leg_timeout'] = 900.0
            out = os.path.join(d, 'out-' + how)
            t0 = time.time()
            with contextlib.redirect_stdout(io.StringIO()):
                rc = perfrun.multi_run(game, opts['legs'], out, None, opts)
            with open(os.path.join(out, 'M001', 'run.json'), encoding='utf-8') as f:
                why = json.load(f).get('error', '')
            check(rc == 1 and time.time() - t0 < 30 and want in why and ('placed',) in calls,
                  f'a `next` that {how}: the leg failed at once ({why!r}), the run ended and put back')
    finally:
        for k, v in saved.items():
            setattr(drive, k, v)
        for k, v in gsaved.items():
            setattr(gen, k, v)
        perfrun._sample = sample_saved
        perfrun.wait_population = population_saved
        import shutil
        shutil.rmtree(d, ignore_errors=True)


scenario_files()
log_turnover()
both_plugins()
outlived_runs()
ini_option()
scenarios()
report_parsing()
probe_parsing()
perfrun_probe_options()
vanilla_names()
drive_during()
rewrite_files()
legs_options()
legs_logs()
wave_ready()
legs_run()
print(f'{len(failures)} failed' if failures else 'all passed')
sys.exit(1 if failures else 0)
