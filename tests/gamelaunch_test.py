"""The test launch of the game (pylib/gamelaunch.py) offline: no game, no Steam, no window is touched.

  - Steam's console_log: the 2026-11-26 14:07 hang (SynchronizingControllerConfig "syncfailed" never answered) is a
    pending launch, said with its step; launches Steam answered itself (ShowInterstitials / CreatingProcess wait and
    continue at once) or that completed / failed are not; neither are other apps' nor an earlier Steam session's;
  - the launch refuses up front (the game or its launcher already running, no Steam, a pending Steam launch) without
    starting anything, and never opens a steam:// URL;
  - it starts LaunchGame.exe from the game folder with SteamAppId / SteamGameId, takes the EDF6.exe whose parent is
    that LaunchGame.exe (not another EDF6.exe), returns at its main window;
  - each failure is said at once (a message box with its text, LaunchGame.exe gone without a game, the game gone
    before its window, the deadline) and closes what this launch started, and only that: the game's own quit first,
    TerminateProcess only when it does not go.
"""
from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
import gamelaunch  # noqa: E402

GAME = r'X:\game'
STEAM = (1000, 4, 'steam.exe')


def action(n: int, text: str, app: int = 2291060, at: str = '2026-11-26 14:07:07') -> str:
    return f'[{at}] GameAction [AppID {app}, ActionID {n}] : {text}'


COMPLETED = [action(232, t) for t in (
    'LaunchApp changed task to CheckShaderDepotManifest with ""',
    'LaunchApp changed task to SynchronizingCloud with ""',
    'LaunchApp changed task to ShowInterstitials with ""',
    'LaunchApp waiting for user response to ShowInterstitials ""',
    'LaunchApp continues with user response "ShowInterstitials"',
    'LaunchApp changed task to SynchronizingControllerConfig with ""',
    'LaunchApp changed task to CreatingProcess with ""',
    'LaunchApp waiting for user response to CreatingProcess ""',
    'LaunchApp continues with user response "CreatingProcess"',
    'LaunchApp changed task to WaitingGameWindow with ""',
    'LaunchApp changed task to Completed with ""')]
# 2026-11-26 14:07 as console_log.txt has it (the later steam:// launches only log ExecuteSteamURL)
HUNG = [action(233, t) for t in (
    'LaunchApp changed task to SynchronizingCloud with ""',
    'LaunchApp changed task to ShowInterstitials with ""',
    'LaunchApp waiting for user response to ShowInterstitials ""',
    'LaunchApp continues with user response "ShowInterstitials"',
    'LaunchApp changed task to SynchronizingControllerConfig with ""',
    'LaunchApp waiting for user response to SynchronizingControllerConfig "syncfailed"')] + [
    '[2026-11-26 14:09:46] ExecCommandLine: "D:\\steam\\steam.exe -- steam://rungameid/2291060"',
    '[2026-11-26 14:09:46] ExecuteSteamURL: "steam://rungameid/2291060"']
SESSION = '[2026-11-26 15:00:00] CStartupModeManager::ComputeStartupMode: forcing desktopui via -silent'


class SteamLogTest(unittest.TestCase):
    def test_the_unanswered_dialog_is_pending_with_its_step(self):
        said = gamelaunch.pending_steam_launch('\n'.join(COMPLETED + HUNG))
        self.assertIsNotNone(said)
        self.assertIn('SynchronizingControllerConfig "syncfailed"', said)
        self.assertIn('233', said)

    def test_answered_completed_and_failed_launches_are_not(self):
        failed = [action(240, 'LaunchApp changed task to SynchronizingCloud with ""'),
                  action(240, 'LaunchApp failed with AppError_16 with ""'),
                  action(240, 'LaunchApp changed task to Failed with ""')]
        self.assertIsNone(gamelaunch.pending_steam_launch('\n'.join(COMPLETED + failed)))
        self.assertIsNone(gamelaunch.pending_steam_launch(''))

    def test_a_launch_under_way_is_pending(self):
        said = gamelaunch.pending_steam_launch('\n'.join(COMPLETED + HUNG[:1]))
        self.assertIn('launching', said)

    def test_other_apps_and_earlier_steam_sessions_do_not_count(self):
        other = [action(9, 'LaunchApp waiting for user response to SynchronizingCloud "syncfailed"', app=570)]
        self.assertIsNone(gamelaunch.pending_steam_launch('\n'.join(other)))
        self.assertIsNone(gamelaunch.pending_steam_launch('\n'.join(HUNG + [SESSION] + COMPLETED)))
        self.assertIsNotNone(gamelaunch.pending_steam_launch('\n'.join([SESSION] + HUNG)))


class RefusalTest(unittest.TestCase):
    def test_refusals(self):
        self.assertIsNone(gamelaunch.refusal([STEAM], None))
        self.assertIn('EDF6.exe is running', gamelaunch.refusal([STEAM, (5, 1, 'EDF6.exe')], None))
        self.assertIn('LaunchGame.exe is running', gamelaunch.refusal([STEAM, (5, 1, 'launchgame.exe')], None))
        self.assertIn('Steam is not running', gamelaunch.refusal([], None))
        self.assertEqual(gamelaunch.refusal([STEAM], 'pending'), 'pending')

    def test_environment_carries_the_app_id(self):
        env = gamelaunch.environment({'PATH': 'x'})
        self.assertEqual((env['SteamAppId'], env['SteamGameId'], env['PATH']), ('2291060', '2291060', 'x'))

    def test_edf_dll_stamp(self):
        with tempfile.TemporaryDirectory() as game:
            head = bytearray(0x200)
            head[0:2] = b'MZ'
            head[0x3C:0x40] = (0x80).to_bytes(4, 'little')
            head[0x80:0x84] = b'PE\0\0'
            head[0x88:0x8C] = gamelaunch.EDF_DLL_STAMP.to_bytes(4, 'little')
            with open(os.path.join(game, 'EDF.dll'), 'wb') as f:
                f.write(head)
            self.assertEqual(gamelaunch.game_dll_stamp(game), gamelaunch.EDF_DLL_STAMP)
            self.assertFalse(gamelaunch.request_exit(1, os.path.join(game, 'none')))   # no EDF.dll: never written

    def test_real_process_list(self):
        procs = gamelaunch.processes()
        self.assertIn((os.getpid(), os.getppid()), [(p, parent) for p, parent, _ in procs])


class FakeWorld:
    """processes / windows / Popen / the clock as one launch would see them, step by step."""

    def __init__(self, steps: list[tuple[list, list]], launcher_exits_at: int | None = None, before: bool = False):
        self.before = before   # the first step is there before the launch too (the refusals)
        self.steps = steps   # per poll: (processes beyond Steam, windows)
        self.i = 0
        self.launcher_exits_at = launcher_exits_at
        self.clock = 0.0
        self.abandoned: list = []   # (pid, launcher) of each abandon
        self.popen = Mock()
        self.started = False   # nothing but Steam runs before the launch (else it is refused up front)
        self.popen.side_effect = lambda *a, **k: setattr(self, 'started', True) or self.popen.return_value
        self.popen.return_value.pid = 100
        self.popen.return_value.returncode = 3
        self.popen.return_value.poll.side_effect = lambda: (
            3 if self.launcher_exits_at is not None and self.i >= self.launcher_exits_at else None)

    def processes(self):
        if not self.started and not self.before:
            return [STEAM]
        procs, _ = self.steps[min(self.i, len(self.steps) - 1)]
        return [STEAM] + procs

    def windows(self, pids):
        _, wins = self.steps[min(self.i, len(self.steps) - 1)]
        return [w for w in wins if w[1] in pids]

    def sleep(self, s):
        self.i += 1
        self.clock += s

    def monotonic(self):
        return self.clock

    def run(self, timeout: float = 90.0, on_window=None):
        with patch.object(gamelaunch, 'processes', side_effect=self.processes), \
             patch.object(gamelaunch, 'windows', side_effect=self.windows), \
             patch.object(gamelaunch, 'steam_log_pending', return_value=None), \
             patch.object(gamelaunch, 'dialog_text', side_effect=lambda h: f'box {h}: data files missing'), \
             patch.object(gamelaunch, 'abandon', side_effect=lambda *a, **k: self.abandoned.append(a[1:3])), \
             patch.object(gamelaunch.os.path, 'isfile', return_value=True), \
             patch.object(gamelaunch.subprocess, 'Popen', self.popen), \
             patch.object(gamelaunch.time, 'sleep', side_effect=self.sleep), \
             patch.object(gamelaunch.time, 'monotonic', side_effect=self.monotonic), \
             patch('modfiles.steam_running_app', return_value=0), \
             patch.object(gamelaunch.os, 'startfile', create=True) as startfile:
            try:
                return gamelaunch.launch(GAME, timeout, on_window=on_window, say=lambda s: None)
            finally:
                startfile.assert_not_called()   # never a steam:// URL


LAUNCHER = (100, 50, 'LaunchGame.exe')
OURS = (200, 100, 'EDF6.exe')
MAIN = (0xABC, 200, gamelaunch.WINDOW_CLASS, 'EarthDefenceForce 6 for PC')


class LaunchTest(unittest.TestCase):
    def test_starts_launchgame_from_the_folder_and_returns_at_the_window(self):
        shown = []
        world = FakeWorld([([LAUNCHER], []), ([LAUNCHER, OURS], []), ([OURS], [MAIN])], launcher_exits_at=2)
        game = world.run(on_window=shown.append)
        self.assertEqual((game.pid, game.hwnd, game.launcher), (200, 0xABC, 100))
        self.assertEqual(shown, [0xABC])
        self.assertEqual(world.abandoned, [])
        args, kwargs = world.popen.call_args
        self.assertEqual(args[0], [os.path.join(GAME, 'LaunchGame.exe')])
        self.assertEqual(kwargs['cwd'], GAME)
        self.assertEqual(kwargs['env']['SteamAppId'], '2291060')
        self.assertEqual(kwargs['stdout'], subprocess.DEVNULL)

    def test_refused_up_front_starts_nothing(self):
        for procs in ([(7, 1, 'EDF6.exe')], [(7, 1, 'LaunchGame.exe')]):
            world = FakeWorld([(procs, [])], before=True)
            with self.assertRaises(gamelaunch.LaunchFailed):
                world.run()
            world.popen.assert_not_called()
        world = FakeWorld([([], [])])
        with patch.object(gamelaunch, 'steam_log_pending', return_value='Steam launch 233 waits'):
            with patch.object(gamelaunch, 'processes', side_effect=world.processes), \
                 patch.object(gamelaunch.subprocess, 'Popen', world.popen):
                with self.assertRaisesRegex(gamelaunch.LaunchFailed, '233 waits'):
                    gamelaunch.launch(GAME)
        world.popen.assert_not_called()

    def test_another_edf6_is_not_taken_for_ours(self):
        # an EDF6.exe whose parent is not our LaunchGame.exe (it cannot be there: refused up front; if one turns up
        # meanwhile it is not this launch's) never counts; ours then comes
        stray = (300, 77, 'EDF6.exe')
        stray_main = (0xDEF, 300, gamelaunch.WINDOW_CLASS, 'EarthDefenceForce 6 for PC')
        world = FakeWorld([([STEAM], []), ([LAUNCHER, stray], [stray_main]), ([LAUNCHER, stray, OURS], [stray_main, MAIN])])
        world.steps[0] = ([LAUNCHER], [])
        self.assertEqual(world.run().pid, 200)

    def test_a_message_box_fails_at_once_with_its_text(self):
        box = (0x55, 200, gamelaunch.DIALOG_CLASS, '错误： 验证游戏文件的完整性')
        world = FakeWorld([([LAUNCHER, OURS], [MAIN, box])])
        with self.assertRaisesRegex(gamelaunch.LaunchFailed, 'data files missing') as e:
            world.run()
        self.assertEqual(world.i, 0)   # no waiting at all
        self.assertIn('message box', str(e.exception))
        self.assertEqual(world.abandoned, [(200, 100)])

    def test_failures_close_what_this_launch_started(self):
        cases = {
            'without starting': (FakeWorld([([LAUNCHER], []), ([], [])], launcher_exits_at=1), None),
            'exited before its window': (FakeWorld([([LAUNCHER, OURS], []), ([], [])]), 200),
            'no game window in 10 s': (FakeWorld([([LAUNCHER, OURS], [])]), 200),
        }
        for said, (world, pid) in cases.items():
            with self.assertRaisesRegex(gamelaunch.LaunchFailed, said):
                world.run(timeout=10)
            self.assertLess(world.clock, 11, said)
            self.assertEqual(world.abandoned, [(pid, 100)], said)


class AbandonTest(unittest.TestCase):
    def run_abandon(self, procs_seq, exit_ok):
        seq = iter(procs_seq)
        last = [procs_seq[-1]]

        def procs():
            try:
                last[0] = next(seq)
            except StopIteration:
                pass
            return last[0]
        clock = [0.0]
        with patch.object(gamelaunch, 'processes', side_effect=procs), \
             patch.object(gamelaunch, 'request_exit', return_value=exit_ok) as ask, \
             patch.object(gamelaunch, 'terminate', return_value=True) as end, \
             patch.object(gamelaunch.time, 'sleep', side_effect=lambda s: clock.__setitem__(0, clock[0] + s)), \
             patch.object(gamelaunch.time, 'monotonic', side_effect=lambda: clock[0]):
            gamelaunch.abandon(GAME, 200, 100, say=lambda s: None, wait=30)
        return ask, end

    def test_the_games_own_quit_first(self):
        ask, end = self.run_abandon([[OURS], [OURS], []], exit_ok=True)
        ask.assert_called_once_with(200, GAME)
        end.assert_not_called()

    def test_ended_when_it_does_not_quit(self):
        ask, end = self.run_abandon([[OURS]], exit_ok=True)
        end.assert_called_once_with(200)
        ask, end = self.run_abandon([[OURS]], exit_ok=False)
        end.assert_called_once_with(200)

    def test_a_still_running_launcher_is_ended_and_nothing_else(self):
        ask, end = self.run_abandon([[LAUNCHER]], exit_ok=True)
        end.assert_called_once_with(100)
        ask.assert_not_called()
        ask, end = self.run_abandon([[(200, 1, 'notepad.exe')]], exit_ok=True)   # pid reused by something else
        end.assert_not_called()
        ask.assert_not_called()


if __name__ == '__main__':
    unittest.main()
