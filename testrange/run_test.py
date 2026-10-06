"""One-command in-game test: install the range (optional), start EDF6, walk the menus into the test
mission, run in-mission keys, collect the plugin log, quit. Screenshots only at checkpoints.

    python run_test.py                          # use what is installed now, 60 s in the mission
    python run_test.py --heli                   # Air Raider + N9 Eros in the vehicle slot, no script vehicles
    python run_test.py --seconds 120 --act "wait:5 key:2 hold:mouse1:1500 wait:30"
    python run_test.py --cdb                    # attach cdb: an access violation writes tmp dump + stack
    python run_test.py --keep                   # leave the game running at the end

Every key goes through `front()` first: nothing is sent unless EDF6 owns the foreground window.
Output: run_<time>/ with shots, the new plugin log lines (log.txt) and summary.txt.
"""
from __future__ import annotations

import argparse
import ctypes
import ctypes.wintypes as wt
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen  # noqa: E402
import weapons  # noqa: E402

user32 = ctypes.windll.user32
user32.SetProcessDPIAware()

CDB = r'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
LOG = ('Mods', 'Plugins', 'EDF6VehicleCrew.log')
SCAN = {'enter': 0x1C, 'esc': 0x01, 'space': 0x39, 'tab': 0x0F, 'w': 0x11, 'a': 0x1E, 's': 0x1F, 'd': 0x20,
        'e': 0x12, 'q': 0x10, 'r': 0x13, 'f': 0x21, 'x': 0x2D, 'z': 0x2C, 'c': 0x2E, 'v': 0x2F, 'g': 0x22, 'b': 0x30,
        'shift': 0x2A, 'ctrl': 0x1D, 'alt': 0x38, 'f4': 0x3E, '1': 0x02, '2': 0x03, '3': 0x04, '4': 0x05,
        '5': 0x06, 'up': (0x48, True), 'down': (0x50, True), 'left': (0x4B, True), 'right': (0x4D, True),
        'home': (0x47, True), 'pgup': (0x49, True), 'pgdn': (0x51, True)}
MOUSE = {'mouse1': (0x02, 0x04), 'mouse2': (0x08, 0x10)}

# Menu navigation is driven by what is on screen, never by blind timing: each checkpoint is a crop
# of a 1600x900 frame compared with refs/<name>.png (mean grey difference after a 4x downscale; a
# match scores ~0, other screens 11+). `footer` is the mod banner shown on every HQ-side screen.
REFS = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'refs')
# checkpoint -> [(reference file, crop box)]; the menu background alternates between a blue and a
# brown theme, which moves the HQ title and recolours the footer.
CHECK = {
    'footer': [('footer_blue', (160, 808, 580, 830)), ('footer_brown', (160, 808, 580, 830))],
    'hq': [('hq_blue', (225, 52, 400, 88)), ('hq_brown', (690, 45, 910, 82))],
}
LIMIT = {'footer': 10, 'hq': 6, 'mission': 10}
BRIEFING = (730, 425, 1000, 500)   # the selected mission's briefing text (the preview above it is a video)
HELI_LOADOUT = {'enabled': True, 'class': 2, 'slots': ['', '', '', '', 'eWeapon394'], 'stars': 10,
                'refill': True}
# Two helis spawned with NPC pilots (a generated `_mission` SGO, see gen.DERIVED): a formation.
HELI_PLAN_FRIENDS: dict[str, int] = {'edf6tr_v506_heli_mission': 2}

KEYS_OF_INTEREST = ('HOOK', 'LOADOUT', 'CREW', 'HELI', 'BUMP', 'ERROR', 'WARN', 'crash', 'fail')


# ---------- window / input ----------
def edf_pid() -> int | None:
    out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq EDF6.exe', '/FO', 'CSV', '/NH'],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.strip('"').split('","')
        if len(parts) > 1 and parts[0].lower() == 'edf6.exe':
            return int(parts[1])
    return None


def window_of(pid: int) -> int | None:
    found: list[int] = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(hwnd: int, _: int) -> bool:
        p = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if p.value == pid and user32.IsWindowVisible(hwnd) and not user32.GetWindow(hwnd, 4):
            r = wt.RECT()
            user32.GetWindowRect(hwnd, ctypes.byref(r))
            if r.right - r.left > 300:
                found.append(hwnd)
        return True
    user32.EnumWindows(cb, 0)
    return found[0] if found else None


def foreground_pid() -> int:
    p = wt.DWORD()
    user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), ctypes.byref(p))
    return p.value


def front(pid: int) -> bool:
    hwnd = window_of(pid)
    if not hwnd:
        return False
    if foreground_pid() != pid:
        user32.keybd_event(0x12, 0, 0, 0)          # an Alt tap lets SetForegroundWindow through
        user32.SetForegroundWindow(hwnd)
        user32.keybd_event(0x12, 0, 2, 0)
        time.sleep(0.5)
    return foreground_pid() == pid


class GameGone(Exception):
    pass


class Runner:
    def __init__(self, out_dir: str, pid: int) -> None:
        self.out = out_dir
        self.pid = pid
        self.notes: list[str] = []

    def alive(self) -> bool:
        return edf_pid() == self.pid

    def guard(self) -> None:
        if not self.alive():
            raise GameGone('EDF6 进程已退出')
        if not front(self.pid):
            raise RuntimeError('EDF6 不在前台，拒绝发键')

    def key(self, name: str, hold: float = 0.08, gap: float = 0.12) -> None:
        self.guard()
        codes = []
        for k in name.split('+'):
            v = SCAN[k]
            codes.append(v if isinstance(v, tuple) else (v, False))
        for code, ext in codes:
            user32.keybd_event(0, code, 0x8 | (0x1 if ext else 0), 0)
        time.sleep(hold)
        for code, ext in reversed(codes):
            user32.keybd_event(0, code, 0xA | (0x1 if ext else 0), 0)
        time.sleep(gap)

    def mouse(self, name: str, hold: float) -> None:
        self.guard()
        down, up = MOUSE[name]
        user32.mouse_event(down, 0, 0, 0, 0)
        time.sleep(hold)
        user32.mouse_event(up, 0, 0, 0, 0)
        time.sleep(0.12)

    def press(self, name: str, down: bool) -> None:
        """A key or mouse button pressed (`down`) or let go, held across the steps between (shots while held)."""
        if down:
            self.guard()
        if name in MOUSE:
            user32.mouse_event(MOUSE[name][0 if down else 1], 0, 0, 0, 0)
        else:
            code = SCAN[name]
            code, ext = code if isinstance(code, tuple) else (code, False)
            user32.keybd_event(0, code, (0x8 if down else 0xA) | (0x1 if ext else 0), 0)
        time.sleep(0.05)

    def look(self, dx: int, dy: int, steps: int = 20) -> None:
        self.guard()
        for i in range(steps):
            user32.mouse_event(0x1, dx * (i + 1) // steps - dx * i // steps, dy * (i + 1) // steps - dy * i // steps, 0, 0)
            time.sleep(0.015)
        time.sleep(0.2)

    def wait(self, seconds: float) -> None:
        end = time.time() + seconds
        while time.time() < end:
            if not self.alive():
                raise GameGone('EDF6 进程已退出')
            time.sleep(min(1.0, end - time.time()) if end > time.time() else 0)

    def frame(self):
        """The client area as a 1600-wide RGB image, or None without a window."""
        from PIL import Image
        gdi32 = ctypes.windll.gdi32
        hwnd = window_of(self.pid)
        if not hwnd:
            return None
        r = wt.RECT()
        user32.GetClientRect(hwnd, ctypes.byref(r))
        w, h = r.right, r.bottom
        hdc = user32.GetDC(hwnd)
        mem = gdi32.CreateCompatibleDC(hdc)
        bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
        gdi32.SelectObject(mem, bmp)
        user32.PrintWindow(hwnd, mem, 3)

        class BMI(ctypes.Structure):
            _fields_ = [('biSize', wt.DWORD), ('biWidth', ctypes.c_long), ('biHeight', ctypes.c_long),
                        ('biPlanes', wt.WORD), ('biBitCount', wt.WORD), ('biCompression', wt.DWORD),
                        ('biSizeImage', wt.DWORD), ('a', ctypes.c_long), ('b', ctypes.c_long),
                        ('c', wt.DWORD), ('d', wt.DWORD)]
        bmi = BMI(ctypes.sizeof(BMI), w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
        buf = ctypes.create_string_buffer(w * h * 4)
        gdi32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(bmi), 0)
        gdi32.DeleteObject(bmp)
        gdi32.DeleteDC(mem)
        user32.ReleaseDC(hwnd, hdc)
        img = Image.frombuffer('RGBA', (w, h), buf, 'raw', 'BGRA', 0, 1).convert('RGB')
        return img.resize((1600, round(h * 1600 / w))) if w != 1600 else img

    def screen(self, mission: str = '') -> dict[str, float]:
        """Best difference score per checkpoint for the current frame (lower = closer); `mission`
        compares the briefing text with refs/<mission>.png."""
        from PIL import Image, ImageChops, ImageStat
        img = self.frame()
        checks = dict(CHECK)
        if mission and os.path.isfile(os.path.join(REFS, mission + '.png')):
            checks['mission'] = [(mission, BRIEFING)]
        if img is None or img.size != (1600, 900):
            return {k: 999.0 for k in checks}
        self.last = img
        out = {}
        for k, refs in checks.items():
            best = 999.0
            for name, box in refs:
                ref = Image.open(os.path.join(REFS, name + '.png')).convert('L')
                size = (max(1, ref.size[0] // 4), max(1, ref.size[1] // 4))
                crop = img.crop(box).convert('L').resize(size)
                best = min(best, ImageStat.Stat(ImageChops.difference(crop, ref.resize(size))).mean[0])
            out[k] = best
        return out

    def on(self, name: str, scores: dict[str, float] | None = None) -> bool:
        return (scores or self.screen()).get(name, 999.0) < LIMIT[name]

    def shot(self, name: str) -> str | None:
        img = self.frame()
        if img is None:
            return None
        path = os.path.join(self.out, name + '.png')
        img.save(path)
        self.notes.append(f'shot {path}')
        return path

    def act(self, script: str) -> None:
        """`wait:S key:NAME[:MS] hold:mouse1:MS down:NAME up:NAME look:DX:DY shot:NAME repeat:N:KEY` separated by spaces.
        look moves the mouse by DX, DY counts in small steps (camera turn; +DY looks down)."""
        for step in script.split():
            kind, _, rest = step.partition(':')
            if kind == 'wait':
                self.wait(float(rest))
            elif kind == 'key':
                name, _, ms = rest.partition(':')
                self.key(name, int(ms) / 1000 if ms else 0.08)
            elif kind == 'hold':
                name, _, ms = rest.partition(':')
                self.mouse(name, int(ms) / 1000)
            elif kind == 'shot':
                self.shot(rest)
            elif kind in ('down', 'up'):
                self.press(rest, kind == 'down')
            elif kind == 'look':
                dx, _, dy = rest.partition(':')
                self.look(int(dx), int(dy or 0))
            elif kind == 'repeat':
                n, _, name = rest.partition(':')
                for _ in range(int(n)):
                    self.key(name)
            else:
                raise ValueError(f'unknown step {step}')


# ---------- log ----------
def log_path(game_root: str) -> str:
    return os.path.join(game_root, *LOG)


def log_size(game_root: str) -> int:
    p = log_path(game_root)
    return os.path.getsize(p) if os.path.isfile(p) else 0


def log_since(game_root: str, offset: int) -> list[str]:
    p = log_path(game_root)
    if not os.path.isfile(p):
        return []
    with open(p, 'rb') as f:
        if os.path.getsize(p) < offset:   # the plugin truncated it on start
            offset = 0
        f.seek(offset)
        return f.read().decode('utf-8', 'replace').splitlines()


def summarize(lines: list[str]) -> list[str]:
    out = []
    for tag in ('HOOK', 'LOADOUT', 'CREW', 'HELI', 'BUMP'):
        hit = [l for l in lines if tag in l]
        out.append(f'{tag}: {len(hit)} 行')
        out += ['    ' + l for l in hit[:4]]
        if len(hit) > 8:
            out.append('    ...')
            out += ['    ' + l for l in hit[-4:]]
    out += aim_stats(lines)
    bad = [l for l in lines if any(t in l for t in ('ERROR', 'WARN', 'fail'))]
    if bad:
        out.append(f'错误/警告: {len(bad)} 行')
        out += ['    ' + l for l in bad[:10]]
    return out


def aim_stats(lines: list[str]) -> list[str]:
    """Per-second HELI samples with a target: how often the guns fire and how far off the nose was."""
    rows = []
    for l in lines:
        m = re.search(r'target=(\w+) dist=(\d+) off=\S+ miss=([\d.]+)deg pitch=(-?\d+)deg gun=(\d) msl=(\d)', l)
        if m and int(m.group(1), 16) and ' land ' not in l:
            rows.append((float(m.group(2)), float(m.group(3)), int(m.group(4)), int(m.group(5)), int(m.group(6))))
    if not rows:
        return ['瞄准：没有交战样本']
    mid = lambda xs: sorted(xs)[len(xs) // 2]
    return [f'瞄准：交战样本 {len(rows)} 个，机枪开火 {sum(r[3] for r in rows)} 个，导弹 {sum(r[4] for r in rows)} 个；'
            f'距离中位数 {mid([r[0] for r in rows]):.0f} 米，机头偏差中位数 {mid([r[1] for r in rows]):.1f} 度，'
            f'俯仰中位数 {mid([r[2] for r in rows])} 度']


# ---------- flow ----------
STEAM_APP = 2291060

def steam_running_app() -> int:
    """Steam's own idea of the running game (HKCU RunningAppID); it lags the process exit by a
    few seconds, and a launch inside that window only gets the 「游戏已在运行」 dialog."""
    import winreg
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam') as k:
            return int(winreg.QueryValueEx(k, 'RunningAppID')[0])
    except OSError:
        return 0


def start_game(timeout: float) -> int:
    if edf_pid():
        raise RuntimeError('EDF6 已经在运行；先关掉它（或加 --attach 接着用这个进程）')
    end = time.time() + 60
    while steam_running_app() == STEAM_APP and time.time() < end:
        time.sleep(1)
    if steam_running_app() == STEAM_APP:
        raise RuntimeError('Steam 仍认为 EDF6 在运行（RunningAppID 60 秒没清零）')
    os.startfile(f'steam://rungameid/{STEAM_APP}')
    end = time.time() + timeout
    while time.time() < end:
        pid = edf_pid()
        if pid and window_of(pid):
            return pid
        time.sleep(1)
    raise RuntimeError('等不到 EDF6 窗口')


def attach_cdb(pid: int, out_dir: str) -> subprocess.Popen:
    dump = os.path.join(out_dir, 'crash.dmp').replace('\\', '/')
    cmds = os.path.join(out_dir, 'cdb_cmds.txt')
    with open(cmds, 'w') as f:
        f.write(f'sxd -c2 "r; kn 60; lm; .dump /ma {dump}; qd" av\ng\n')
    return subprocess.Popen([CDB, '-p', str(pid), '-cf', cmds], stdout=open(os.path.join(out_dir, 'cdb.txt'), 'w'),
                            stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)


def navigate(r: Runner, slot: gen.Slot, timeout: float, trace: bool, learn: bool) -> None:
    """Title -> the test range's mission on the last used difficulty.

    Green title/save/mode menus all take Enter, so Enter is pressed every 3 s until the HQ-side
    footer shows. From there every key depends on the recognised screen. On the mission list the
    cursor runs to the top, steps down to the slot's item, and Enter is only pressed once the
    briefing text matches refs/<mission>.png. `learn` saves that reference instead (first run)."""
    end = time.time() + timeout
    last_enter = 0.0
    tries = 0
    step = 0
    while time.time() < end:
        s = r.screen(slot.mission)
        if trace:
            r.shot(f'0_nav{step:02d}')
            r.notes.append(f'nav{step:02d} ' + ' '.join(f'{k}={v:.0f}' for k, v in s.items()))
        step += 1
        if r.on('mission', s):
            r.key('enter')           # -> difficulty dialog, opened on the last used difficulty
            r.wait(1.5)
            r.key('enter')
            return
        if r.on('hq', s):
            r.key('up', 1.5)         # held: the cursor runs to the top item 「出击」
            r.key('enter')
            r.wait(2.5)
            continue
        if r.on('footer', s):        # mission list: top, then down to the slot's item
            tries += 1
            if tries > 3:            # not the list after all: back out to the HQ menu
                r.key('esc')
                r.wait(2)
                tries = 0
                continue
            r.key('up', 6.0)
            r.wait(0.3)
            for _ in range(slot.item - 1):
                r.key('down', 0.08, 0.3)
            r.wait(1.0)
            if learn:
                r.screen()
                r.last.crop(BRIEFING).save(os.path.join(REFS, slot.mission + '.png'))
                r.shot('learn_' + slot.mission)
                raise RuntimeError(f'已保存 refs/{slot.mission}.png，先看 learn_{slot.mission}.png 确认光标在第 {slot.item} 项')
            continue
        if time.time() - last_enter > 3:
            r.key('enter')
            last_enter = time.time()
        r.wait(0.7)
    raise RuntimeError('菜单导航超时（画面一直没认出来；用 --trace 看每一步截图）')


QUIT_YES = (645, 462, 690, 486)   # the 「是」 button of the Alt+F4 dialog left of its text (1600-wide frame)


def yes_selected(r: Runner) -> bool:
    """The selected button of the quit dialog is green, the other one blue (measured: green - blue is
    about +57 selected, -20 not)."""
    from PIL import ImageStat
    img = r.frame()
    if img is None or img.size != (1600, 900):
        return False
    red, green, blue = ImageStat.Stat(img.crop(QUIT_YES)).mean
    return green > blue + 25


def quit_game(r: Runner) -> bool:
    """Alt+F4 opens the game's quit dialog with 「否」 selected. The title menus move the selection with
    Left, a mission only with A (arrow keys do nothing there), so both are tried until 「是」 shows
    selected; Enter confirms, Enter again on the 「即将结束游戏」 notice. True once the process is gone."""
    try:
        r.key('alt+f4', 0.15)
        r.wait(1.5)
        for spec in ('left', 'a', 'left', 'a'):
            if yes_selected(r):
                break
            r.key(spec, 0.3)
            r.wait(0.8)
        if not yes_selected(r):
            r.shot('quit_dialog')
            r.key('esc', 0.15)
            return False
        r.key('enter', 0.2)
        r.wait(2)
        r.key('enter', 0.2)
        r.wait(20)
    except (GameGone, RuntimeError):
        pass
    return not r.alive()


def install(args: argparse.Namespace) -> list[str]:
    if not (args.heli or args.plan or args.slot):
        return ['测试场/装备：保持现在装的']
    plan = gen.load_plan(args.plan) if args.plan else gen.Plan()
    if args.heli:
        plan.vehicles = {}
        plan.friends = dict(HELI_PLAN_FRIENDS)
        plan.loadout = dict(HELI_LOADOUT)
        plan.waves.enabled = args.enemies
    if args.slot:
        plan.slot = args.slot
    lines = gen.install(args.game, plan) or ['（没有脚本载具）']
    lines += weapons.write_loadout(args.game, plan.loadout) or ['装备：存档里的']
    return lines


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', default=os.environ.get('EDF6_DIR', gen.DEFAULT_GAME))
    ap.add_argument('--plan', help='testrange.json 一类的方案文件；不给就不重装')
    ap.add_argument('--heli', action='store_true', help='空袭兵；地图上生成两架 NPC 驾驶的 506 直升机（编队）；载具格仍是 N9 Eros')
    ap.add_argument('--slot', choices=[x.mission for x in gen.SLOTS], help='测试场装进哪一关（默认沿用方案/现装的）')
    ap.add_argument('--enemies', action='store_true', help='和 --heli 一起用：也刷敌人波次')
    ap.add_argument('--seconds', type=float, default=60, help='进关后停留秒数（--act 跑完后剩余时间）')
    ap.add_argument('--act', default='', help='进关后的动作序列，见 Runner.act')
    ap.add_argument('--menu-timeout', type=float, default=180, help='从启动到选好难度最多等多少秒')
    ap.add_argument('--load-wait', type=float, default=25, help='选完难度后等关卡载入的秒数')
    ap.add_argument('--cdb', action='store_true')
    ap.add_argument('--learn', action='store_true', help='第一次用某个槽位：走到那一项后保存说明文字参考图并停下')
    ap.add_argument('--trace', action='store_true', help='菜单每一步都截图（采集参考图用）')
    ap.add_argument('--keep', action='store_true', help='结束时不退出游戏')
    ap.add_argument('--attach', action='store_true', help='用已经在跑的 EDF6（停在标题画面）')
    args = ap.parse_args()

    out = os.path.join(gen.HERE, 'runs', time.strftime('%Y%m%d_%H%M%S'))
    os.makedirs(out, exist_ok=True)
    summary = ['安装：'] + ['    ' + l for l in install(args)]
    slot = gen.installed(args.game)
    if slot is None:
        raise RuntimeError('游戏里没装测试场')
    summary.append(f'槽位：{slot.mission}（任务列表第 {slot.item} 项）')
    offset = log_size(args.game)
    t0 = time.time()
    stage = 'start'
    pid = edf_pid() if args.attach else start_game(120)
    if not pid:
        raise RuntimeError('没有在跑的 EDF6')
    r = Runner(out, pid)
    dbg = attach_cdb(pid, out) if args.cdb else None
    try:
        stage = 'menu'
        navigate(r, slot, args.menu_timeout, args.trace, args.learn)
        stage = 'loading'
        r.wait(args.load_wait)
        r.shot('2_in_mission')
        stage = 'act'
        if args.act:
            r.act(args.act)
        r.wait(args.seconds)
        r.shot('3_end')
        stage = 'done'
    except GameGone:
        summary.append(f'!! 游戏在「{stage}」阶段退出（崩溃？）')
    except Exception as e:  # reported, the game is still closed below
        summary.append(f'!! {stage}: {e!r}')
        r.shot('fail')
    finally:
        if not args.keep and not quit_game(r):
            summary.append('!! 游戏没有退出（见 quit_dialog.png），下次运行前请手动关掉')
        if dbg:
            time.sleep(2)
            if dbg.poll() is None:
                dbg.terminate()
    lines = log_since(args.game, offset)
    with open(os.path.join(out, 'log.txt'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))
    summary += [f'阶段：{stage}，用时 {time.time() - t0:.0f} 秒', f'插件日志新增 {len(lines)} 行'] + summarize(lines)
    if os.path.isfile(os.path.join(out, 'crash.dmp')):
        summary.append('!! cdb 抓到访问违例：crash.dmp / cdb.txt')
    summary += r.notes
    text = '\n'.join(summary)
    with open(os.path.join(out, 'summary.txt'), 'w', encoding='utf-8') as f:
        f.write(text)
    print(text)
    print('输出目录', out)
    return 0 if stage == 'done' else 1


if __name__ == '__main__':
    sys.exit(main())
