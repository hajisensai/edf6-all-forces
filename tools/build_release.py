"""Builds the unzip-and-run release: release/EDF6VehicleCrew-<version>.zip holding
  EDF6VehicleCrew安装器.exe  (tools/installer.py frozen by PyInstaller, plugin dll + ini inside)
  说明.txt
Run build.cmd first (the exe bundles dist/Mods/Plugins/EDF6VehicleCrew.dll as it is now).
Usage: python tools/build_release.py [version]
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
PLUGINS = os.path.join(ROOT, 'dist', 'Mods', 'Plugins')
EXE_NAME = 'EDF6VehicleCrew安装器'
OUT = os.path.join(ROOT, 'release')
WORK = os.path.join(ROOT, 'build', 'pyinstaller')

README = """EDF6VehicleCrew（空中支援 / 载具乘员插件）

前提：游戏目录里已经装好 EDFModLoader（winmm.dll + ModLoader.ini + Mods 文件夹）。

安装 / 更新：
  1. 先退出游戏。
  2. 双击「EDF6VehicleCrew安装器.exe」，输入 1 回车。
     自动找 Steam 里的游戏目录；找不到会让你粘贴游戏目录（有 EDF6.exe 的那个文件夹）。
     也可以把 exe 放进游戏目录里再运行。
  3. 看到「安装完成」后启动游戏。

  不需要装 Python。战机、直升机、潜水母舰和呼叫武器是安装器用你自己游戏里的
  Root.cpk 现场生成的（不修改 Root.cpk）；武器表只追加本插件的行，别的 mod 的行原样保留。
  已有的 Mods/Plugins/EDF6VehicleCrew.ini 不会被覆盖（你的设置保留）。

卸载：
  运行安装器输入 2。
  选 1 = 连呼叫武器一起删：必须先在所有存档里卸下这些武器，否则删掉后游戏会在主菜单崩溃。
  选 2 = 只删插件：武器留着，会按原版轰炸机呼叫，不影响游玩。

杀毒软件可能误报 PyInstaller 打包的 exe，这是打包方式本身的常见误报。
"""


def build_exe() -> str:
    seps = os.pathsep
    paths = [os.path.join(ROOT, 'tools'), os.path.join(ROOT, 'testrange'),
             os.path.join(ROOT, 'testrange', 'lib'), os.path.join(ROOT, 'autoturret', 'tools')]
    cmd = [sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean', '--onefile', '--console',
           '--name', EXE_NAME, '--distpath', os.path.join(WORK, 'dist'), '--workpath', os.path.join(WORK, 'work'),
           '--specpath', WORK]
    for p in paths:
        cmd += ['--paths', p]
    for mod in ('call_weapons', 'make_jets', 'make_sub'):
        cmd += ['--hidden-import', mod]
    for mod in ('numpy', 'PIL', 'matplotlib', 'pandas', 'tkinter'):  # dev-only tools import these
        cmd += ['--exclude-module', mod]
    for name in ('EDF6VehicleCrew.dll', 'EDF6VehicleCrew.ini'):
        cmd += ['--add-data', f'{os.path.join(PLUGINS, name)}{seps}plugin']
    cmd.append(os.path.join(ROOT, 'tools', 'installer.py'))
    subprocess.run(cmd, check=True)
    return os.path.join(WORK, 'dist', EXE_NAME + '.exe')


def main(argv: list[str]) -> int:
    version = argv[0] if argv else 'dev'
    for name in ('EDF6VehicleCrew.dll', 'EDF6VehicleCrew.ini'):
        if not os.path.isfile(os.path.join(PLUGINS, name)):
            raise SystemExit(f'missing {name} in dist/Mods/Plugins: run build.cmd first')
    exe = build_exe()
    os.makedirs(OUT, exist_ok=True)
    zpath = os.path.join(OUT, f'EDF6VehicleCrew-{version}.zip')
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED) as z:
        z.write(exe, EXE_NAME + '.exe')
        z.writestr('说明.txt', README.replace('\n', '\r\n').encode('utf-8-sig'))
    shutil.copy2(exe, os.path.join(OUT, EXE_NAME + '.exe'))
    print(f'wrote {zpath} ({os.path.getsize(zpath)} bytes)')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
