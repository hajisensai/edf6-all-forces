"""Offline driver regressions: no game process, window or installation is touched."""
import ctypes
import builtins
import sys
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
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
        none = {'extra': (), 'loadout': None, 'shots': None, 'keys': (), 'place': (), 'ini': ()}
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
                                    place=(), ini=())
        with self.assertRaises(SystemExit):
            drive.run_options(['--key', '57'])
        with self.assertRaises(SystemExit):
            drive.run_options(['--cmd'])

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
             patch.object(drive, 'game_window', return_value=123), patch.object(drive, 'hide'):
            drive.launch()
        start.assert_called_once_with('steam://rungameid/2291060')
        u.PostMessageW.assert_not_called()

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
