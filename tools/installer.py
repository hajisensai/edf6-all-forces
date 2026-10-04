"""EDF6VehicleCrew installer / uninstaller: the program the release zip ships (built into an .exe by
tools/build_release.py), so players do not need Python.

What install does, with EDF6.exe closed:
  1. finds the game directory (tools gamedir: next to the exe, then the Steam libraries) or asks for it;
  2. copies EDF6VehicleCrew.dll into Mods/Plugins (the .ini only when there is none yet: it keeps the
     player's settings);
  3. generates the jets, helicopters, drones, submarine carrier and call weapons from the player's own
     Root.cpk (make_jets, make_sub, call_weapons install). They cannot be shipped prebuilt: they are
     derived from the game's files, and the weapon table is shared with other mods, so our rows are
     appended to whatever table this install already has.

Uninstall removes only our own files and our own weapon-table rows (call_weapons keeps the others).
"""
from __future__ import annotations

import os
import shutil
import sys
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
for _p in (HERE, os.path.join(HERE, '..', 'testrange'), os.path.join(HERE, '..', 'testrange', 'lib'),
           os.path.join(HERE, '..', 'autoturret', 'tools')):
    sys.path.insert(0, os.path.normpath(_p))

import gamedir  # noqa: E402

PLUGIN = 'EDF6VehicleCrew'
PROCESS = 'EDF6.exe'


def bundle_dir() -> str:
    """Where the shipped plugin files are: inside the onefile exe, else the repo's dist/."""
    if getattr(sys, 'frozen', False):
        return os.path.join(sys._MEIPASS, 'plugin')  # type: ignore[attr-defined]
    return os.path.normpath(os.path.join(HERE, '..', 'dist', 'Mods', 'Plugins'))


def game_running() -> bool:
    import subprocess
    r = subprocess.run(['tasklist', '/FI', f'IMAGENAME eq {PROCESS}', '/NH'],
                       capture_output=True, text=True, errors='replace')
    return PROCESS.lower() in r.stdout.lower()


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


def install_plugin(game: str) -> None:
    src = bundle_dir()
    dst = os.path.join(game, 'Mods', 'Plugins')
    os.makedirs(dst, exist_ok=True)
    shutil.copy2(os.path.join(src, PLUGIN + '.dll'), os.path.join(dst, PLUGIN + '.dll'))
    print(f'写入 {os.path.join(dst, PLUGIN + ".dll")}')
    ini = os.path.join(dst, PLUGIN + '.ini')
    if os.path.isfile(ini):
        print(f'保留已有的 {ini}（你的设置不覆盖）')
    else:
        shutil.copy2(os.path.join(src, PLUGIN + '.ini'), ini)
        print(f'写入 {ini}')


def remove_plugin(game: str) -> None:
    for ext in ('.dll', '.ini', '.log'):
        path = os.path.join(game, 'Mods', 'Plugins', PLUGIN + ext)
        if os.path.isfile(path):
            os.remove(path)
            print(f'删除 {path}')


def install(game: str) -> None:
    import call_weapons
    import make_jets
    import make_sub
    check_loader(game)
    install_plugin(game)
    print('\n生成战机、直升机、无人机（读取 Root.cpk，不修改它）……')
    make_jets.main([game])
    print('\n生成潜水母舰（约 39 MB）……')
    make_sub.main([game])
    print('\n追加空袭兵呼叫武器到武器表（只追加本插件的行，其它行不动）……')
    call_weapons.install(game)
    print('\n安装完成。启动游戏即可。')


def uninstall(game: str) -> None:
    import call_weapons
    import make_jets
    import make_sub
    print('卸载会从武器表里删掉本插件的呼叫武器。')
    print('如果有存档的兵种还装备着这些呼叫武器，删掉后进游戏会在主菜单崩溃。')
    print('只删插件、保留武器的话，这些武器会照原版 KM6 轰炸机呼叫，不影响游玩。')
    choice = ask('输入 1 = 我已在所有存档卸下这些武器，连武器一起删；输入 2 = 只删插件（武器和生成的模型留着）；其它 = 取消：')
    if choice not in ('1', '2'):
        print('已取消。')
        return
    if choice == '1':   # the call weapons point at the generated SGOs: those go only with the rows
        call_weapons.uninstall(game, unequipped=True, force=False)
        make_sub.remove(game)
        make_jets.main([game, '--remove'])
    remove_plugin(game)
    print('\n卸载完成。')


def main(argv: list[str]) -> int:
    print(f'== {PLUGIN} 安装程序 ==\n')
    mode = argv[0] if argv else ''
    if mode not in ('install', 'uninstall'):
        pick = ask('输入 1 安装 / 更新，2 卸载，回车退出：')
        mode = {'1': 'install', '2': 'uninstall'}.get(pick, '')
        if not mode:
            return 0
    if game_running():
        print(f'{PROCESS} 正在运行。请先退出游戏再运行本程序（本程序不会替你关游戏）。')
        return 1
    game = pick_game()
    if not game:
        return 1
    (install if mode == 'install' else uninstall)(game)
    return 0


def run() -> int:
    try:
        code = main(sys.argv[1:])
    except SystemExit as e:  # the tools report refusals this way
        if e.code not in (None, 0):
            print(f'\n未完成：{e.code}')
        code = 0 if e.code in (None, 0) else 1
    except Exception:
        traceback.print_exc()
        print('\n出错了，上面是错误信息。游戏文件可能只写了一部分，修好后重新运行安装即可覆盖。')
        code = 1
    if getattr(sys, 'frozen', False):
        ask('\n按回车关闭窗口……')
    return code


if __name__ == '__main__':
    sys.exit(run())
