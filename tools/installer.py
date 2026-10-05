"""EDF6VehicleCrew installer / uninstaller: the program the release zip ships (built into an .exe by
tools/build_release.py), so players do not need Python.

What install does, with EDF6.exe closed:
  1. finds the game directory (pylib/gamedir.py: next to the exe, then the Steam libraries) or asks for it;
  2. makes everything first, in memory, from the player's own Root.cpk (only read): the call weapons stacked
     onto the shared weapon table (call_weapons.stack, which also checks the table and its texts line up and
     offers repair when they do not), the jets, helicopters and drones (make_jets.build) and the submarine
     carrier (make_sub.build), the ejection's parachute canopy (make_chute.build), the big map
     (make_bigmap.build: the test range's plain stitched 3 x 3, seamless).
     They cannot be shipped prebuilt: they are derived from the game's files, and the
     weapon table is shared with other mods. Nothing is written unless all of it could be made;
  3. writes them: the generated objects (each file atomically, recorded in the ownership ledger,
     pylib/ledger.py), then the weapon table, its texts and the call SGOs in one transaction (all or none,
     call_weapons.commit), and last the plugin: EDF6VehicleCrew.dll, and the .ini (a new one when there is
     none; else the player's own, with only the settings this version adds appended: merge_ini); then the big map
     (it sets BigWorld in that ini) and the test range's grand battle mission (testrange/gen.py, on its slot), so
     everyone in an online room has the same map and the same objects (the user, 2026-10-05: one pack to play with
     others). The test range's forced loadout is never written: everyone picks their own class.

Uninstall removes the plugin and, when asked, the call weapons (their rows become placeholders that keep the
row numbers saves use) and the generated objects no other tool still needs.

Menu 3 downloads the newest build from the test site and menu 4 sends the logs back to it (tools/testhub.py;
the site itself is testhub/).
"""
from __future__ import annotations

import os
import re
import sys
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
for _p in (HERE, os.path.join(HERE, '..', 'pylib'), os.path.join(HERE, '..', 'testrange')):
    sys.path.insert(0, os.path.normpath(_p))

import gamedir  # noqa: E402
import modfiles  # noqa: E402

PLUGIN = 'EDF6VehicleCrew'
PROCESS = modfiles.PROCESS
SECTION = 'VehicleCrew'
ADDED_HEADER = '; ---- 新版本新增的设置（安装器补入，默认值）----'


def bundle_dir() -> str:
    """Where the shipped plugin files are: inside the onefile exe, else the build output (build.cmd)."""
    if getattr(sys, 'frozen', False):
        return os.path.join(sys._MEIPASS, 'plugin')  # type: ignore[attr-defined]
    return os.path.normpath(os.path.join(HERE, '..', 'build', 'Mods', 'Plugins'))


def plugin_files() -> tuple[bytes, bytes]:
    """The plugin DLL and its default ini, as shipped (or as build.cmd built them)."""
    src = bundle_dir()
    paths = [os.path.join(src, PLUGIN + ext) for ext in ('.dll', '.ini')]
    missing = [p for p in paths if not os.path.isfile(p)]
    if missing:
        raise SystemExit(f'找不到 {", ".join(missing)}：先运行 build.cmd 构建插件')
    out = []
    for p in paths:
        with open(p, 'rb') as f:
            out.append(f.read())
    return out[0], out[1]


def ask(prompt: str) -> str:
    try:
        return input(prompt).strip()
    except EOFError:
        return ''


def pick_game() -> str | None:
    found = gamedir.find()
    if found:
        print(f'游戏目录：{found}')
        return found
    print('没有自动找到游戏目录（需要同时有 EDF6.exe 和 Root.cpk）。')
    while True:
        path = ask('请把游戏目录粘贴到这里后回车（直接回车退出）：').strip('"')
        if not path:
            return None
        if gamedir.is_game(path):
            return os.path.normpath(path)
        print('这个目录里没有 EDF6.exe 和 Root.cpk，请重新输入。')


def check_loader(game: str) -> None:
    if not (os.path.isfile(os.path.join(game, 'winmm.dll')) or os.path.isfile(os.path.join(game, 'ModLoader.ini'))):
        print('！ 没有发现 EDFModLoader（游戏目录里的 winmm.dll / ModLoader.ini）。'
              '本插件靠它加载，请先装好 EDFModLoader，否则装了也不会生效。')


# ---------------------------------------------------------------- the ini

_KEY = re.compile(r'^\s*([A-Za-z0-9_]+)\s*=')
_SECTION = re.compile(r'^\s*\[([^\]]+)\]')


def _keys(lines: list[str]) -> dict[str, int]:
    """Key (lower case) -> its line, within [VehicleCrew]."""
    out: dict[str, int] = {}
    section = ''
    for i, line in enumerate(lines):
        m = _SECTION.match(line)
        if m:
            section = m.group(1).strip()
            continue
        m = _KEY.match(line)
        if m and section == SECTION:
            out.setdefault(m.group(1).lower(), i)
    return out


def merge_ini(user: str, shipped: str) -> tuple[str, list[str], list[str]]:
    """The player's ini with every setting of the shipped one it lacks appended to [VehicleCrew] (each with
    the comment lines above it in the shipped file), and nothing of theirs changed. Returns (text, keys added,
    keys of theirs the shipped ini no longer has: the plugin ignores them)."""
    nl = '\r\n' if '\r\n' in user else '\n'
    have = _keys(user.splitlines())
    ship_lines = shipped.splitlines()
    ship = _keys(ship_lines)
    added: list[str] = []
    block: list[str] = []
    for key, i in ship.items():
        if key in have:
            continue
        j = i
        while j > 0 and ship_lines[j - 1].lstrip().startswith(';') and not ship_lines[j - 1].lstrip().startswith('; ----'):
            j -= 1
        block += ship_lines[j:i + 1]
        added.append(_KEY.match(ship_lines[i]).group(1))
    gone = [k for k in (_KEY.match(user.splitlines()[i]).group(1) for i in have.values()) if k.lower() not in ship]
    if not added:
        return user, added, gone
    lines = user.splitlines()
    heads = [(i, m.group(1).strip()) for i, m in ((i, _SECTION.match(x)) for i, x in enumerate(lines)) if m]
    start = next((i for i, name in heads if name == SECTION), None)
    if start is None:
        lines.append(f'[{SECTION}]')
        start = len(lines) - 1
    end = next((i for i, _ in heads if i > start), len(lines))   # [VehicleCrew] runs to the next section
    while end > start + 1 and not lines[end - 1].strip():
        end -= 1
    insert = ['', ADDED_HEADER, *block]
    merged = lines[:end] + insert + lines[end:]
    return nl.join(merged) + nl, added, gone


def install_plugin(game: str, dll: bytes, shipped_ini: bytes) -> None:
    dst = os.path.join(game, 'Mods', 'Plugins')
    modfiles.atomic_write(os.path.join(dst, PLUGIN + '.dll'), dll)
    print(f'写入 {os.path.join(dst, PLUGIN + ".dll")}')
    ini = os.path.join(dst, PLUGIN + '.ini')
    if not os.path.isfile(ini):
        modfiles.atomic_write(ini, shipped_ini)
        print(f'写入 {ini}')
        return
    with open(ini, 'rb') as f:
        raw = f.read()
    bom = raw.startswith(b'\xef\xbb\xbf')
    user = raw[3:].decode('utf-8') if bom else raw.decode('utf-8', errors='surrogateescape')
    text, added, gone = merge_ini(user, shipped_ini.decode('utf-8'))
    if added:
        modfiles.atomic_write(ini, (b'\xef\xbb\xbf' if bom else b'') + text.encode('utf-8', errors='surrogateescape'))
        print(f'保留你的 {ini}，补入新版本新增的设置：{", ".join(added)}')
    else:
        print(f'保留你的 {ini}（没有需要补的新设置）')
    if gone:
        print(f'  其中 {", ".join(gone)} 新版本已不再使用，可以手动删掉')


def remove_plugin(game: str) -> None:
    for ext in ('.dll', '.ini', '.log'):
        path = os.path.join(game, 'Mods', 'Plugins', PLUGIN + ext)
        if os.path.isfile(path):
            os.remove(path)
            print(f'删除 {path}')


# ---------------------------------------------------------------- install / uninstall


def misaligned(game: str, error: Exception) -> bool:
    """Explains call_weapons.Misaligned (the weapon texts no longer line up with the table). True when
    call_weapons.repair can fix it (a first install kept the files as they were), after warning what a repair
    loses."""
    import call_weapons
    print(f'\n！ {error}')
    if not os.path.isfile(os.path.join(game, 'Mods', call_weapons.MANIFEST)):
        return False
    later = call_weapons.changed_since(game, call_weapons.load_manifest(game), call_weapons.SHARED)
    if later:
        print('  注意：这些文件在本插件上次安装之后被别的工具改过，恢复会丢掉那些改动（之后重新运行那个 MOD 的安装即可补回）：'
              + '、'.join(later))
    return True


def repair_weapons(game: str) -> None:
    import call_weapons
    for rel in call_weapons.repair(game):
        print(f'恢复 {rel}')


def stack_weapons(game: str) -> dict[str, bytes] | None:
    """The call weapons stacked onto the weapon table; when its texts no longer line up, offers to put the
    files back as they were before the first install (call_weapons.repair) and stacks again. None: cancelled."""
    import call_weapons
    try:
        return call_weapons.stack(game)
    except call_weapons.Misaligned as e:
        if not misaligned(game, e):
            return None
        if ask('输入 y 把武器表和武器说明恢复到第一次安装本插件之前的样子，然后继续安装；其它 = 取消：').lower() != 'y':
            return None
        repair_weapons(game)
        return call_weapons.stack(game)


def retire_weapons(game: str) -> bool:
    """Turns the call weapons into placeholders (call_weapons.uninstall). When the weapon texts no longer line
    up with the table, offers what install offers (repair: the files back to before the first install, which
    takes the call weapons out too), or to leave the table alone and remove only the plugin and the generated
    objects. False: cancelled, nothing changed."""
    import call_weapons
    try:
        call_weapons.uninstall(game)
        return True
    except call_weapons.Misaligned as e:
        repairable = misaligned(game, e)
    print('  也可以跳过武器表：武器表和武器说明一个字节都不动，只删插件和生成的模型（呼叫武器的行留着，照原版 KM6 呼叫；'
          '玩家喷气机请求要用的模型仍被这些行登记着，会保留）。')
    prompt = ('输入 y 把武器表和武器说明恢复到第一次安装本插件之前的样子（呼叫武器随之去掉），然后继续卸载；'
              if repairable else '') + '输入 s 跳过武器表继续卸载；其它 = 取消：'
    choice = ask(prompt).lower()
    if choice == 'y' and repairable:
        repair_weapons(game)
        return True
    return choice == 's'


def install(game: str) -> None:
    import call_weapons
    import gen
    import make_artillery
    import make_bigmap
    import make_drill
    import make_chute
    import make_jets
    import make_katyusha
    import make_sub
    check_loader(game)
    dll, ini = plugin_files()
    if call_weapons.recover(game):
        print('上次运行没有完成：已把武器表相关文件恢复到那次运行之前。')
    print('检查武器表并生成呼叫武器（只读 Root.cpk 与现有武器表）……')
    weapons = stack_weapons(game)
    if weapons is None:
        print('已取消，没有写入任何文件。')
        return
    print('生成战机、直升机、无人机（读取 Root.cpk，不修改它）……')
    jets = make_jets.build(game)
    print('生成潜水母舰（约 39 MB）……')
    sub = make_sub.build(game)
    print('生成喀秋莎火箭炮车（读取 Root.cpk，不修改它）……')
    katyusha = make_katyusha.build(game)
    print('生成自行榴弹炮（读取 Root.cpk，不修改它）……')
    artillery = make_artillery.build(game)
    print('生成降落伞（读取 Root.cpk，不修改它）……')
    chute = make_chute.build(game)
    print('生成钻头战车（读取 Root.cpk 和钻头战车模型，不修改它们）……')
    drill = make_drill.build(game)
    print('生成大地图（测试场平原拼成 3 x 3，无缝；读取 Root.cpk，不修改它，约需一两分钟）……')
    bigmap = make_bigmap.build(game)
    print('\n全部生成完毕，开始写入。')
    for path in make_jets.install(game, jets) + make_sub.install(game, sub) + make_katyusha.install(game, katyusha) + make_artillery.install(game, artillery) + \
            make_chute.install(game, chute) + make_drill.install(game, drill):
        print('写入', path)
    print('写入呼叫武器（武器表只动本插件的行，其它行不动；全部写完或全部不写）……')
    call_weapons.install(game, weapons)
    install_plugin(game, dll, ini)
    for path in make_bigmap.install(game, built=bigmap):
        print('写入', path)
    print('写入测试场「大混战」关卡（联机时大家要有同样的关卡和物体）……')
    for line in gen.install(game, gen.grand_battle(gen.Plan())):
        print('  ', line)
    print('\n安装完成。启动游戏即可。')


def uninstall(game: str) -> None:
    import gen
    import make_artillery
    import make_bigmap
    import make_drill
    import make_chute
    import make_jets
    import make_katyusha
    import make_sub
    print('卸载会删掉插件。呼叫武器可以一起删：武器表里它们的行会变成「已卸载」的占位行，')
    print('效果和原版 KM6 轰炸机呼叫（玩家喷气机请求则是原版 N9 Eros）相同，行号保住，存档装备着也不会崩溃。')
    choice = ask('输入 1 = 插件和呼叫武器、生成的模型一起删；输入 2 = 只删插件（武器和生成的模型留着，照原版 KM6 呼叫）；其它 = 取消：')
    if choice not in ('1', '2'):
        print('已取消。')
        return
    if choice == '1':   # the call weapons point at the generated SGOs: those go only with the rows
        if not retire_weapons(game):
            print('已取消，没有删除任何文件。')
            return
        for remove in (make_drill.remove, make_chute.remove, make_artillery.remove, make_katyusha.remove, make_sub.remove, make_jets.remove):
            deleted, kept = remove(game)
            for path in deleted:
                print('删除', path)
            for path in kept:
                print('保留（之后被别的工具改过）', path)
    if gen.uninstall(game):
        print('删除测试场关卡')
    for path in make_bigmap.remove(game)[0]:
        print('删除', path)
    remove_plugin(game)
    print('\n卸载完成。')


def build_name() -> str:
    """This installer's build: build_release.py bundles build_info.json (the zip's name without .zip)."""
    import json
    try:
        with open(os.path.join(bundle_dir(), 'build_info.json'), encoding='utf-8') as f:
            return json.load(f).get('name', '')
    except (OSError, ValueError):
        return ''


def download_latest() -> int:
    """Menu 3: the newest build from the test site, unpacked next to this exe; offers to start its installer."""
    import testhub
    hub = testhub.hub_for_player(ask)
    here = os.path.dirname(os.path.abspath(sys.executable if getattr(sys, 'frozen', False) else __file__))
    exe = testhub.download_latest(hub, here, build_name())
    if exe and ask('输入 y 现在打开新版安装器；其它 = 不打开：').lower() == 'y':
        os.startfile(exe)  # type: ignore[attr-defined]
        print('新版安装器已在另一个窗口打开，这个窗口可以关掉了。')
    return 0


def send_logs() -> int:
    """Menu 4: the logs and a note back to the developer (the game may be running: the logs are opened shared)."""
    import testhub
    game = pick_game()
    if not game:
        return 1
    testhub.send_logs(testhub.hub_for_player(ask), game, ask, build_name())
    return 0


def main(argv: list[str]) -> int:
    print(f'== {PLUGIN} 安装程序 {build_name()} ==\n')
    mode = argv[0] if argv else ''
    if mode not in ('install', 'uninstall', 'update', 'logs'):
        pick = ask('输入 1 安装 / 更新，2 卸载，3 下载最新测试版，4 回传日志给开发者，回车退出：')
        mode = {'1': 'install', '2': 'uninstall', '3': 'update', '4': 'logs'}.get(pick, '')
        if not mode:
            return 0
    if mode in ('update', 'logs'):
        import testhub
        try:
            return download_latest() if mode == 'update' else send_logs()
        except testhub.HubError as e:
            print(f'\n没有完成：{e}')
            return 1
    if modfiles.game_running():
        print(f'{PROCESS} 正在运行。请先退出游戏再运行本程序（本程序不会替你关游戏）。')
        return 1
    game = pick_game()
    if not game:
        return 1
    (install if mode == 'install' else uninstall)(game)
    return 0


def offer_error_report(text: str) -> None:
    """After an installer error: the error and the logs to the test site, if the player wants."""
    if not getattr(sys, 'frozen', False) or ask('输入 y 把这个错误和日志发给开发者；其它 = 不发：').lower() != 'y':
        return
    try:
        import testhub
        hub = testhub.hub_for_player(ask)
        files = [('installer-error.txt', text.encode('utf-8'))]
        game = gamedir.find()
        if game:
            files.append(('logs.zip', testhub.collect(game, build_name())[0]))
        rid = testhub.post_report(hub, None, 'new', '安装器出错（自动回传）', build_name(), files)
        print(f'已发送（#{rid}）。')
    except Exception as e:   # a courtesy: its failure must not hide the error above
        print(f'发送失败：{e}')


def run() -> int:
    try:
        code = main(sys.argv[1:])
    except SystemExit as e:  # the tools report refusals this way
        if e.code not in (None, 0):
            print(f'\n未完成：{e.code}')
        code = 0 if e.code in (None, 0) else 1
    except Exception:
        traceback.print_exc()
        print('\n出错了，上面是错误信息。武器表和武器说明要么全部写入、要么已恢复原样；'
              '生成的模型每个文件要么是新的要么是旧的。问题解决后重新运行安装即可。')
        offer_error_report(traceback.format_exc())
        code = 1
    if getattr(sys, 'frozen', False):
        ask('\n按回车关闭窗口……')
    return code


if __name__ == '__main__':
    sys.exit(run())
