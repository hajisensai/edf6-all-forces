"""tools/perfrun.py and tools/perf_report.py, offline (no game):
  - testrange/gen.py write_scenario rewrites only the range mission's own files, keeping the originals on disk (outside
    the game folder) so a run that dies loses nothing, and restore_scenario puts them back byte for byte (a file that
    was not there is removed again); it refuses, before writing anything, when the range is not installed, the waves'
    enemy is not one the range knows, a generated SGO the plan spawns is not in Mods, or an earlier run's originals
    are still kept (a second write would take the first one's scenario for the original);
  - perfrun's scenarios: grand is the grand battle, swarm's fields reach the waves, a stock mission has no plan;
  - perf_report reads the plugin's PERF rows only inside the window (both row formats), sums GPU engines per type, and
    tells the game thread by System::update on its stack, labels a plugin hook below a phase by its outermost
    plugin frame (not the phase's own time) and ranks the plugin's functions by inclusive and self samples;
  - drive.run's `during` ends the stay as soon as it returns (the loop condition, read from the source).
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
        perf_report.Image = lambda path, classes=True: opened.append(path) or None
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
        check('| A | run | 16.70 | 2.00% | 0 | 30.0 | 50 |' in t, 'the A/B table row')
        check('| B | failed | not sampled |' in t, 'a round without samples is a row, not a crash')


def drive_during() -> None:
    import drive
    src = inspect.getsource(drive.run)
    check('during' in inspect.signature(drive.run).parameters and 'not worker.is_alive()' in src,
          "drive.run's stay ends when `during` returns")


scenario_files()
scenarios()
report_parsing()
drive_during()
print(f'{len(failures)} failed' if failures else 'all passed')
sys.exit(1 if failures else 0)
