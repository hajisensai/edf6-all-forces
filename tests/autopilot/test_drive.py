"""Offline driver regressions: no game process, window or installation is touched."""
import ctypes
import io
import builtins
import sys
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from contextlib import redirect_stdout
from unittest.mock import Mock, patch


spec = importlib.util.spec_from_file_location('autopilot_drive', Path(__file__).with_name('drive.py'))
drive = importlib.util.module_from_spec(spec)
with patch.object(ctypes, 'WinDLL', side_effect=lambda *a, **k: Mock()):
    spec.loader.exec_module(drive)
REAL_WATCH = drive.DesktopWatch


class QuietWatch:
    """DesktopWatch without its sampling thread (user32 is a Mock here)."""
    def start(self):
        return self

    def stop(self):
        pass

    def nudge(self):
        return 'nudged'

    def report(self):
        return 'desktop: quiet'


class DriverTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = str(self.root / 'game')
        self.plugins = Path(drive.plugins(self.game))
        self.plugins.mkdir(parents=True)
        src = self.root / 'build' / 'tools'
        src.mkdir(parents=True)
        (src / (drive.NAME + '.dll')).write_bytes(b'test plugin')
        self.root_patch = patch.object(drive, 'ROOT', str(self.root))
        self.root_patch.start()
        self.addCleanup(self.root_patch.stop)
        # main() queues for the game (pylib/gamelease.py): this scratch game's queue stays in the temp folder.
        queue = patch.dict('os.environ', {'EDF6_QUEUE_DIR': str(self.root / 'queues')})
        queue.start()
        self.addCleanup(queue.stop)
        self.pid_patch = patch.object(drive, 'game_pids', return_value=[])
        self.pids = self.pid_patch.start()
        self.addCleanup(self.pid_patch.stop)
        self.watch_patch = patch.object(drive, 'DesktopWatch', QuietWatch)
        self.watch_patch.start()
        self.addCleanup(self.watch_patch.stop)

    def path(self, ext):
        return self.plugins / (drive.NAME + ext)

    def test_install_preserves_each_existing_file(self):
        for ext in drive.SESSION_EXTS:
            with self.subTest(ext=ext):
                self.path(ext).write_bytes(b'user content')
                with self.assertRaises(RuntimeError):
                    drive.install(self.game)
                self.assertEqual(self.path(ext).read_bytes(), b'user content')
                self.assertFalse(self.path('.session.json').exists())
                self.path(ext).unlink()

    def test_uninstall_requires_ownership(self):
        self.path('.dll').write_bytes(b'user plugin')
        with self.assertRaises(RuntimeError):
            drive.uninstall(self.game)
        self.assertEqual(self.path('.dll').read_bytes(), b'user plugin')

    def test_owned_session_cleans_generated_files(self):
        drive.install(self.game)
        for ext in drive.SESSION_EXTS[1:]:
            self.path(ext).write_text('generated')
        drive.uninstall(self.game)
        self.assertEqual(list(self.plugins.iterdir()), [])

    def test_failed_dll_write_removes_only_our_partial_file(self):
        real_open = builtins.open

        class PartialWriter:
            def __init__(self, f):
                self.f = f

            def __enter__(self):
                return self

            def __exit__(self, *args):
                self.f.close()

            def write(self, data):
                self.f.write(data[:2])
                raise OSError('disk full')

        def fail_write(path, mode='r', *args, **kwargs):
            f = real_open(path, mode, *args, **kwargs)
            return PartialWriter(f) if mode == 'xb' else f

        with patch('builtins.open', side_effect=fail_write):
            with self.assertRaisesRegex(OSError, 'disk full'):
                drive.install(self.game)
        self.assertEqual(list(self.plugins.iterdir()), [])

    def test_exclusive_create_race_preserves_other_file(self):
        real_open = builtins.open

        def race(path, mode='r', *args, **kwargs):
            if mode == 'xb':
                self.path('.dll').write_bytes(b'other installer')
            return real_open(path, mode, *args, **kwargs)

        with patch('builtins.open', side_effect=race):
            with self.assertRaises(FileExistsError):
                drive.install(self.game)
        self.assertEqual(self.path('.dll').read_bytes(), b'other installer')
        self.assertFalse(self.path('.session.json').exists())

    def test_early_game_exit_is_failure_and_cleans_up(self):
        with patch.object(drive, 'launch'), patch.object(drive, 'wait_for', return_value=True), \
             patch.object(drive.time, 'time', side_effect=[0, 1]), patch.object(drive, 'command') as command:
            self.assertEqual(drive.run(self.game, 'RM015', 1, 45, None), 1)
        command.assert_called_once_with(self.game, 'mission RM015 1')
        self.assertEqual(list(self.plugins.iterdir()), [])

    def test_run_defaults_to_the_test_range_pack(self):
        with patch.object(drive.gamedir, 'find_or_dev', return_value=self.game), \
             patch.object(drive, 'run', return_value=0) as run:
            self.assertEqual(drive.main(['run']), 0)
        none = {'extra': (), 'loadout': None, 'shots': None, 'keys': (), 'place': (), 'ini': (), 'hq': False, 'every': 15.0}
        run.assert_called_once_with(self.game, 'range', 1, 45, None, **none)
        with patch.object(drive.gamedir, 'find_or_dev', return_value=self.game), \
             patch.object(drive, 'run', return_value=0) as run:
            self.assertEqual(drive.main(['run', 'RM015', '2']), 0)
        run.assert_called_once_with(self.game, 'RM015', 2, 45, None, **none)

    def test_run_options_after_the_positional_ones(self):
        with patch.object(drive.gamedir, 'find_or_dev', return_value=self.game), \
             patch.object(drive, 'run', return_value=0) as run:
            self.assertEqual(drive.main(['run', 'RM015', '1', '90', 'out.log', '--cmd', 'probe airdrop',
                                         '--loadout', 'x.ini', '--cmd', 'mem', '--shots', 'shots',
                                         '--key', '57@25:5000', '--key', 'enter@30']), 0)
        run.assert_called_once_with(self.game, 'RM015', 1, 90, 'out.log', extra=('probe airdrop', 'mem'),
                                    loadout='x.ini', shots='shots', keys=(('57', 25.0, 5000), ('enter', 30.0, 1000)),
                                    place=(), ini=(), hq=False, every=15.0)
        self.assertEqual(drive.run_options(['--every', '1.5'])[1]['every'], 1.5)
        with self.assertRaises(SystemExit):
            drive.run_options(['--every', '0'])
        with self.assertRaises(SystemExit):
            drive.run_options(['--key', '57'])
        with self.assertRaises(SystemExit):
            drive.run_options(['--cmd'])

    def test_cycle_options(self):
        with patch.object(drive.gamedir, 'find_or_dev', return_value=self.game), \
             patch.object(drive, 'run', return_value=0) as run:
            self.assertEqual(drive.main(['run', 'RM015', '1', '1800', '--cycle', '20', '--ini', 'HavokMultithread=1',
                                         '--cycle-stay', '30', '--cycle-reboot']), 0)
        run.assert_called_once_with(self.game, 'RM015', 1, 1800, None, extra=(), loadout=None, shots=None, keys=(),
                                    place=(), ini=('HavokMultithread=1',), hq=False, every=15.0,
                                    cycle=(20, 30.0, True))
        self.assertEqual(drive.run_options(['--cycle', '5'])[1]['cycle'], (5, 20.0, False))
        self.assertNotIn('cycle', drive.run_options([])[1])
        for bad in (['--cycle', '0'], ['--cycle', 'x'], ['--cycle', '10001'], ['--cycle'],
                    ['--cycle', '3', '--cycle-stay', '0'], ['--cycle', '3', '--cycle-stay', 'x'],
                    ['--cycle-stay', '30'], ['--cycle-reboot'], ['--cycle', '3', '--hq']):
            with self.assertRaises(SystemExit, msg=bad):
                drive.run_options(bad)

    def test_cycle_command_text(self):
        self.assertEqual(drive.cycle_command(20, 20.0, False), 'cycle 20 20')
        self.assertEqual(drive.cycle_command(2, 45.5, True), 'cycle 2 45.5 reboot')

    def test_cycle_outcome_and_report_from_the_plugins_rows(self):
        log = ['[   1.0 s] CYCLE armed: leave and enter again 2 times, 20.0 s in each mission, back in by PlayMission_Offline',
               '[  40.0 s] CYCLE 0/2 entered: the mission runs (row 13)',
               '[  60.0 s] CYCLE 1/2 leaving: MissionMgr::End(ABORT), as the pause menu\'s withdraw, after 20.0 s',
               '[  61.0 s] CYCLE 1/2 left: Result_Offline skipped (no result screen, nothing saved) -> PlayMission_Offline',
               '[  90.0 s] CYCLE 1/2 entered: the mission runs (row 13)']
        self.assertIsNone(drive.cycle_outcome(log))
        self.assertEqual(drive.cycle_report(log, 2, None), 'cycle NOT finished in time: left 1 of 2, entered 2 '
                                                           '(the first mission included)')
        done = log + ['[ 111.0 s] CYCLE 2/2 left: Result_Offline skipped (no result screen, nothing saved) -> PlayMission_Offline',
                      '[ 140.0 s] CYCLE 2/2 entered: the mission runs (row 13)',
                      '[ 140.0 s] CYCLE done: left and entered again 2 times']
        self.assertEqual(drive.cycle_outcome(done), 'done')
        self.assertEqual(drive.cycle_report(done, 2, 'done'), 'cycle done: left 2 of 2, entered 3 (the first mission included)')
        self.assertEqual(drive.cycle_outcome(log + ['[ 70.0 s] CYCLE failed: the script went back to the title (1/2 left)']),
                         'failed')
        self.assertEqual(drive.cycle_outcome(['[ 1.0 s] CYCLE refused: a cycle is already running']), 'failed')

    def cycle_run(self, later: list[str], seconds: int = 600, place: tuple[str, ...] = (), on_launch=None):
        """drive.run with --cycle 3 on a fake clock: the plugin's log is `later` from 30 s into the mission on; the game
        runs from the launch until quit is asked. (return code, commands sent, when quit was asked in s from the mission's start)"""
        clock = [1000.0]
        sent: list[str] = []
        quit_at: list[float] = []

        def command(_game, text):
            sent.append(text)
            if text == 'quit':
                quit_at.append(clock[0] - 1000.0)

        def sleep(s):
            clock[0] += s

        def log(_game):
            return later if clock[0] >= 1030.0 else ['[   1.0 s] CYCLE armed: leave and enter again 3 times']
        launched: list[bool] = []
        self.pids.side_effect = lambda: [42] if launched and not quit_at else []
        def launch():
            launched.append(True)
            if on_launch:
                on_launch()
        with patch.object(drive, 'launch', side_effect=launch),              patch.object(drive, 'wait_for', return_value=True), \
             patch.object(drive, 'command', side_effect=command), patch.object(drive, 'rows', side_effect=log), \
             patch.object(drive.time, 'time', side_effect=lambda: clock[0]), \
             patch.object(drive.time, 'sleep', side_effect=sleep):
            rc = drive.run(self.game, 'RM015', 1, seconds, None, cycle=(3, 20.0, False), place=place)
        return rc, sent, quit_at[0] if quit_at else None

    def test_cycle_run_quits_once_done(self):
        rc, sent, quit_at = self.cycle_run(['[ 300.0 s] CYCLE done: left and entered again 3 times'])
        self.assertEqual(rc, 0)
        self.assertEqual(sent[0], 'mission RM015 1\ncycle 3 20')
        self.assertLess(quit_at, 40)   # as soon as it is done, not at the deadline
        self.assertEqual(list(self.plugins.iterdir()), [])

    def test_cycle_run_fails_on_a_failed_cycle_or_the_deadline(self):
        rc, _, quit_at = self.cycle_run(['[ 300.0 s] CYCLE failed: the script went back to the title (1/3 left)'])
        self.assertEqual(rc, 1)
        self.assertLess(quit_at, 40)
        rc, _, quit_at = self.cycle_run(['[ 300.0 s] CYCLE 1/3 left: ...'], seconds=120)
        self.assertEqual(rc, 1)
        self.assertGreaterEqual(quit_at, 120)

    def test_extra_commands_go_with_the_mission(self):
        with patch.object(drive, 'launch'), patch.object(drive, 'wait_for', return_value=False), \
             patch.object(drive, 'command') as command:
            self.assertEqual(drive.run(self.game, 'RM015', 1, 0, None, extra=('probe airdrop',)), 1)
        command.assert_called_once_with(self.game, 'mission RM015 1\nprobe airdrop')

    def test_loadout_is_placed_for_the_run_and_removed_after(self):
        source = self.root / 'load.ini'
        source.write_bytes(b'[Loadout]\nEnabled=1\n')
        seen = []
        def launch():
            seen.append((self.plugins / drive.LOADOUT).read_bytes())
        with patch.object(drive, 'launch', side_effect=launch), patch.object(drive, 'wait_for', return_value=False):
            self.assertEqual(drive.run(self.game, 'RM015', 1, 0, None, loadout=str(source)), 1)
        self.assertEqual(seen, [b'[Loadout]\nEnabled=1\n'])
        self.assertEqual(list(self.plugins.iterdir()), [])

    def test_existing_loadout_is_never_overwritten(self):
        (self.plugins / drive.LOADOUT).write_bytes(b'the test range')
        source = self.root / 'load.ini'
        source.write_bytes(b'ours')
        with patch.object(drive, 'launch') as launch:
            with self.assertRaises(FileExistsError):
                drive.run(self.game, 'RM015', 1, 0, None, loadout=str(source))
        launch.assert_not_called()
        self.assertEqual((self.plugins / drive.LOADOUT).read_bytes(), b'the test range')
        self.assertEqual([p.name for p in self.plugins.iterdir()], [drive.LOADOUT])

    def test_placed_files_are_put_back_byte_for_byte(self):
        dll = self.plugins / 'EDF6VehicleCrew.dll'
        dll.write_bytes(b'installed plugin')
        ini = self.plugins / 'EDF6VehicleCrew.ini'
        ini.write_bytes(b'\xef\xbb\xbf; comment\r\n[VehicleCrew]\r\nEnabled=1\r\n')
        build = self.root / 'build.dll'
        build.write_bytes(b'our build')
        sgo = self.root / 'X.SGO'
        sgo.write_bytes(b'sgo')
        seen = []
        def launch():
            seen.append((dll.read_bytes(), ini.read_bytes(), (self.plugins.parent / 'OBJECT' / 'X.SGO').read_bytes()))
        with patch.object(drive, 'launch', side_effect=launch), patch.object(drive, 'wait_for', return_value=False):
            drive.run(self.game, 'RM015', 1, 0, None, place=(f'{build}=Plugins/EDF6VehicleCrew.dll', f'{sgo}=OBJECT/X.SGO'),
                      ini=('AirdropTest=2',))
        self.assertEqual(seen[0][0], b'our build')
        self.assertEqual(seen[0][1], b'\xef\xbb\xbf; comment\r\n[VehicleCrew]\r\nEnabled=1\r\nAirdropTest=2\r\n')
        self.assertEqual(seen[0][2], b'sgo')
        self.assertEqual(dll.read_bytes(), b'installed plugin')
        self.assertEqual(ini.read_bytes(), b'\xef\xbb\xbf; comment\r\n[VehicleCrew]\r\nEnabled=1\r\n')
        self.assertFalse((self.plugins.parent / 'OBJECT' / 'X.SGO').exists())
        self.assertEqual(sorted(p.name for p in self.plugins.iterdir()), ['EDF6VehicleCrew.dll', 'EDF6VehicleCrew.ini'])

    def test_placed_file_changed_by_someone_else_is_left(self):
        dll = self.plugins / 'EDF6VehicleCrew.dll'
        dll.write_bytes(b'installed plugin')
        build = self.root / 'build.dll'
        build.write_bytes(b'our build')
        def launch():
            dll.write_bytes(b'another session')
        with patch.object(drive, 'launch', side_effect=launch), patch.object(drive, 'wait_for', return_value=False):
            drive.run(self.game, 'RM015', 1, 0, None, place=(f'{build}=Plugins/EDF6VehicleCrew.dll',))
        self.assertEqual(dll.read_bytes(), b'another session')
        kept = self.plugins / (drive.NAME + '.placed') / 'Plugins__EDF6VehicleCrew.dll'
        self.assertEqual(kept.read_bytes(), b'installed plugin')

    def test_a_game_that_outlives_the_run_is_put_back_by_restore(self):
        """The game still running at the run's end (quit_wait passed): nothing is touched then, everything the run put
        there (the placed plugin, the ini, a new SGO, the loadout, the autopilot) is recorded on disk, a new install is
        refused until it is put back, and `drive.py restore` (a later process: nothing kept in memory) puts it back byte
        for byte, mtimes too."""
        dll = self.plugins / 'EDF6VehicleCrew.dll'
        dll.write_bytes(b'installed plugin')
        ini = self.plugins / 'EDF6VehicleCrew.ini'
        ini.write_bytes(b'[VehicleCrew]\r\nEnabled=1\r\n')
        import os
        os.utime(dll, (1_600_000_000, 1_600_000_000))
        build = self.root / 'build.dll'
        build.write_bytes(b'our build')
        sgo = self.root / 'X.SGO'
        sgo.write_bytes(b'sgo')
        loadout = self.root / 'load.ini'
        loadout.write_bytes(b'ours')
        running = []
        self.pids.side_effect = lambda: list(running)

        def launch():
            running.append(123)
        with patch.object(drive, 'launch', side_effect=launch), patch.object(drive, 'wait_for', return_value=False):
            self.assertEqual(drive.run(self.game, 'RM015', 1, 0, None, place=(f'{build}=Plugins/EDF6VehicleCrew.dll',
                             f'{sgo}=OBJECT/X.SGO'), ini=('Debug=1',), loadout=str(loadout)), 1)
        self.assertEqual(dll.read_bytes(), b'our build')   # the game holds it: not touched
        self.assertTrue(self.path('.dll').exists())
        running.clear()
        with self.assertRaises(RuntimeError) as refused:
            drive.install(self.game)
        self.assertIn('drive.py restore', str(refused.exception))
        with patch.object(drive.gamedir, 'find_or_dev', return_value=self.game):
            self.assertEqual(drive.main(['restore']), 0)
        self.assertEqual(dll.read_bytes(), b'installed plugin')
        self.assertEqual(int(dll.stat().st_mtime), 1_600_000_000)
        self.assertEqual(ini.read_bytes(), b'[VehicleCrew]\r\nEnabled=1\r\n')
        self.assertFalse((self.plugins.parent / 'OBJECT' / 'X.SGO').exists())
        self.assertEqual(sorted(p.name for p in self.plugins.iterdir()), ['EDF6VehicleCrew.dll', 'EDF6VehicleCrew.ini'])
        with patch.object(drive.gamedir, 'find_or_dev', return_value=self.game):
            self.assertEqual(drive.main(['restore']), 0)   # nothing left: nothing to do

    def test_a_run_that_leaves_a_placed_file_fails(self):
        """A run that went well otherwise is non-zero when restore_placed left a file (changed by someone else)."""
        dll = self.plugins / 'EDF6VehicleCrew.dll'
        dll.write_bytes(b'installed plugin')
        build = self.root / 'build.dll'
        build.write_bytes(b'our build')
        done = ['[ 300.0 s] CYCLE done: left and entered again 3 times']
        rc, _, _ = self.cycle_run(done, place=(f'{build}=Plugins/EDF6VehicleCrew.dll',))
        self.assertEqual(rc, 0)   # put back: the run's own outcome
        self.assertEqual(dll.read_bytes(), b'installed plugin')
        rc, _, _ = self.cycle_run(done, place=(f'{build}=Plugins/EDF6VehicleCrew.dll',),
                                  on_launch=lambda: dll.write_bytes(b'another session'))
        self.assertEqual(rc, 1)
        self.assertTrue((self.plugins / (drive.NAME + '.placed') / 'manifest.json').exists())

    def test_unrecorded_files_in_placed_dir_fail_restore(self):
        """An older drive.py's backup (EDF6Autopilot.placed/ without manifest.json, the user's own plugin in it):
        restore says where it is and fails, touching nothing, and install keeps refusing - both see the same thing."""
        placed = self.plugins / (drive.NAME + '.placed')
        placed.mkdir()
        (placed / 'Plugins__EDF6VehicleCrew.dll').write_bytes(b'the user plugin')
        out = io.StringIO()
        with patch.object(drive.gamedir, 'find_or_dev', return_value=self.game), redirect_stdout(out):
            self.assertEqual(drive.main(['restore']), 1)
        self.assertIn('Plugins__EDF6VehicleCrew.dll', out.getvalue())
        self.assertIn('Mods/Plugins/EDF6VehicleCrew.dll', out.getvalue())
        self.assertEqual((placed / 'Plugins__EDF6VehicleCrew.dll').read_bytes(), b'the user plugin')
        with self.assertRaises(RuntimeError):
            drive.install(self.game)
        (placed / 'Plugins__EDF6VehicleCrew.dll').unlink()   # dealt with by hand
        with patch.object(drive.gamedir, 'find_or_dev', return_value=self.game):
            self.assertEqual(drive.main(['restore']), 0)
        self.assertFalse(placed.exists())

    def test_placement_that_failed_before_its_payload_leaves_nothing(self):
        """The manifest write or the copy of the original failing: no copy without an entry, no entry without a
        placement - placed_dir gone, the original untouched, a restore with nothing to do."""
        dll = self.plugins / 'EDF6VehicleCrew.dll'
        dll.write_bytes(b'installed plugin')
        placed = self.plugins / (drive.NAME + '.placed')
        with patch.object(drive, '_write_manifest', side_effect=OSError('disk full')):
            with self.assertRaises(OSError):
                drive.place_file(self.game, 'Plugins/EDF6VehicleCrew.dll', b'our build')
        self.assertFalse(any(p.name.startswith('Plugins__') for p in placed.iterdir()) if placed.exists() else False)
        self.assertEqual(dll.read_bytes(), b'installed plugin')
        self.assertTrue(drive.restore_placed(self.game))
        self.assertFalse(placed.exists())
        with patch.object(drive.shutil, 'copy2', side_effect=OSError('locked')):
            with self.assertRaises(OSError):
                drive.place_file(self.game, 'Plugins/EDF6VehicleCrew.dll', b'our build')
        self.assertFalse(placed.exists())
        self.assertEqual(dll.read_bytes(), b'installed plugin')
        # killed between the entry and the copy: the entry alone, the file still its original - nothing to put back
        real_copy = drive.shutil.copy2
        with patch.object(drive.shutil, 'copy2', side_effect=KeyboardInterrupt):
            with self.assertRaises(KeyboardInterrupt):
                drive.place_file(self.game, 'Plugins/EDF6VehicleCrew.dll', b'our build')
        self.assertFalse(placed.exists())
        self.assertIs(drive.shutil.copy2, real_copy)

    def test_restore_refuses_while_the_game_runs(self):
        build = self.root / 'build.dll'
        build.write_bytes(b'our build')
        drive.install(self.game)
        drive.place_file(self.game, 'Plugins/EDF6VehicleCrew.dll', b'our build')
        self.pids.return_value = [123]
        with self.assertRaises(RuntimeError):
            drive.restore_placed(self.game)
        self.assertEqual((self.plugins / 'EDF6VehicleCrew.dll').read_bytes(), b'our build')
        self.pids.return_value = []
        self.assertTrue(drive.restore_placed(self.game))
        self.assertEqual(list(self.plugins.iterdir()), [])

    def test_loadout_changed_during_the_run_is_left(self):
        source = self.root / 'load.ini'
        source.write_bytes(b'ours')
        def launch():
            (self.plugins / drive.LOADOUT).write_bytes(b'changed')
        with patch.object(drive, 'launch', side_effect=launch), patch.object(drive, 'wait_for', return_value=False):
            drive.run(self.game, 'RM015', 1, 0, None, loadout=str(source))
        self.assertEqual((self.plugins / drive.LOADOUT).read_bytes(), b'changed')

    def test_modified_dll_not_deleted(self):
        drive.install(self.game)
        self.path('.dll').write_bytes(b'new user version')
        with self.assertRaises(RuntimeError):
            drive.uninstall(self.game)
        self.assertEqual(self.path('.dll').read_bytes(), b'new user version')

    def test_running_game_blocks_install_even_under_python_optimized(self):
        self.pids.return_value = [123]
        with self.assertRaises(RuntimeError):
            drive.install(self.game)
        self.assertEqual(list(self.plugins.iterdir()), [])

    def test_failed_initial_command_cleans_install(self):
        with patch.object(drive, 'command', side_effect=OSError('write failed')):
            with self.assertRaises(OSError):
                drive.run(self.game, 'RM015', 1, 0, None)
        self.assertEqual(list(self.plugins.iterdir()), [])

    def test_failed_log_copy_still_cleans_install(self):
        def launch():
            self.path('.log').write_text('generated')
        with patch.object(drive, 'launch', side_effect=launch), patch.object(drive, 'wait_for', return_value=False):
            with self.assertRaises(OSError):
                drive.run(self.game, 'RM015', 1, 0, str(self.root / 'missing' / 'log'))
        self.assertEqual(list(self.plugins.iterdir()), [])

    def test_keys_need_a_running_session(self):
        with self.assertRaises(RuntimeError):
            drive.key(self.game, 'enter', 150)
        self.assertFalse(self.path('.keys').exists())

    def test_interrupted_key_hold_releases_keys(self):
        drive.install(self.game)
        self.pids.return_value = [123]
        with patch.object(drive.time, 'sleep', side_effect=KeyboardInterrupt):
            with self.assertRaises(KeyboardInterrupt):
                drive.key(self.game, 'enter', 150)
        self.assertEqual(self.path('.keys').read_text(), '')

    def test_handle_apis_are_pointer_sized(self):
        for lib, name in ((drive.user32, 'GetForegroundWindow'), (drive.user32, 'GetDC'),
                          (drive.gdi32, 'CreateCompatibleDC'), (drive.gdi32, 'SelectObject')):
            self.assertEqual(ctypes.sizeof(getattr(lib, name).restype), ctypes.sizeof(ctypes.c_void_p))
        self.assertEqual(drive.user32.GetClientRect.argtypes[0], ctypes.wintypes.HWND)

    def test_capture_restores_bitmap_before_read_and_releases_on_failure(self):
        calls = []
        def rect(hwnd, dest):
            dest._obj.right, dest._obj.bottom = 8, 8
            return 1
        u, g = Mock(), Mock()
        u.GetClientRect.side_effect = rect
        u.GetDC.return_value = 0x123456789
        u.PrintWindow.return_value = 1
        g.CreateCompatibleDC.return_value = 0x234567890
        g.CreateCompatibleBitmap.return_value = 0x345678901
        g.SelectObject.side_effect = lambda dc, obj: calls.append(('select', obj)) or 0x456789012
        g.GetDIBits.side_effect = lambda *args: calls.append(('read', args[1])) or 0
        with patch.object(drive, 'user32', u), patch.object(drive, 'gdi32', g), \
             patch.dict(sys.modules, {'PIL': Mock()}):
            with self.assertRaisesRegex(RuntimeError, 'complete image'):
                drive.capture(0x567890123)
        self.assertEqual(calls, [('select', 0x345678901), ('select', 0x456789012), ('read', 0x345678901)])
        g.DeleteObject.assert_called_once_with(0x345678901)
        g.DeleteDC.assert_called_once_with(0x234567890)
        u.ReleaseDC.assert_called_once_with(0x567890123, 0x123456789)

    def test_launch_never_confirms_steam_dialogs(self):
        u = Mock()
        with patch.object(drive, 'user32', u), patch.object(drive.os, 'startfile') as start, \
             patch.object(drive, 'steam_launching_title', return_value=None), \
             patch.object(drive, 'game_window', return_value=123), patch.object(drive, 'hide'):
            drive.launch()
        start.assert_called_once_with('steam://rungameid/2291060')
        u.PostMessageW.assert_not_called()

    def test_steam_launching_title_from_the_clients_localization(self):
        with tempfile.TemporaryDirectory() as steam:
            folder = Path(steam, 'steamui', 'localization')
            folder.mkdir(parents=True)
            folder.joinpath('steamui_schinese-json.js').write_text(
                'var x={"GameAction_Launching":"Launching","GameAction_LaunchingDialog_Title":"\\u542f\\u52a8\\u4e2d...",'
                '"GameAction_LaunchingDialog_Starting":"x"}', encoding='utf-8')
            self.assertEqual(drive.steam_launching_title(steam, 'schinese'), '启动中...')
            self.assertIsNone(drive.steam_launching_title(steam, 'english'))   # no file: nothing is moved

    def test_only_steams_launch_progress_window_is_moved(self):
        t = 'Launching...'
        self.assertTrue(drive.is_steam_launching_window(t, 'SDL_app', 'steamwebhelper.exe', t))
        self.assertTrue(drive.is_steam_launching_window(t, 'SDL_app', 'SteamWebHelper.EXE', t))
        for title, cls, exe, launching in ((t, 'SDL_app', 'steamwebhelper.exe', None),        # title unknown
                                           ('Cloud Sync Conflict', 'SDL_app', 'steamwebhelper.exe', t),
                                           (t, 'SDL_app', 'chrome.exe', t), (t, 'Other', 'steamwebhelper.exe', t)):
            self.assertFalse(drive.is_steam_launching_window(title, cls, exe, launching), (title, cls, exe, launching))

    def test_nudge_tells_a_pulled_cursor_from_a_free_one(self):
        def fake_user32(pull):
            u = Mock()
            at = {'x': 100, 'y': 200}

            def get(p):
                p._obj.x, p._obj.y = (1919, 540) if pull and (at['x'], at['y']) == (140, 240) else (at['x'], at['y'])
                return 1

            def put(x, y):
                at['x'], at['y'] = x, y
                return 1
            u.GetCursorPos.side_effect = get
            u.SetCursorPos.side_effect = put
            u.GetSystemMetrics.side_effect = lambda i: {76: 0, 77: 0, 78: 1920, 79: 1080}[i]
            return u, at
        for pull, word in ((False, 'stayed there'), (True, 'PULLED to the edge in 100/100')):
            with self.subTest(pull=pull):
                u, at = fake_user32(pull)
                with patch.object(drive, 'user32', u), patch.object(drive.time, 'sleep'):
                    verdict = REAL_WATCH.__new__(REAL_WATCH)
                    verdict.nudges = []
                    self.assertIn(word, verdict.nudge())
                self.assertEqual((at['x'], at['y']), (100, 200))   # put back where it was

    def test_process_query_failure_is_not_game_closed(self):
        self.pid_patch.stop()
        with patch.object(drive.subprocess, 'run', side_effect=subprocess.CalledProcessError(1, 'tasklist')):
            with self.assertRaises(subprocess.CalledProcessError):
                drive.install(self.game)
        self.assertEqual(list(self.plugins.iterdir()), [])


if __name__ == '__main__':
    unittest.main()
