"""Builds the unzip-and-run release: release/EDF6VehicleCrew-<version>.zip holding
  EDF6VehicleCrew安装器.exe  (tools/installer.py frozen by PyInstaller, plugin dll + ini inside)
  说明.txt
The version is the one CMakeLists.txt project(VERSION) sets, the same the DLL reports (src/version.h.in).
Run build.cmd first: the exe bundles build/Mods/Plugins/EDF6VehicleCrew.dll and .ini as they are now.
Needs PyInstaller (python -m pip install pyinstaller).

Usage: python tools/build_release.py [--suffix=TEXT] [--expect VERSION]
  --suffix   appended to the file name's version (CI: --suffix=-build.<commit>; with = since it starts with -)
  --expect   fail unless CMakeLists.txt's version is this one (CI: the pushed tag vX.Y.Z)
Prints the zip's path last.
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
PLUGINS = os.path.join(ROOT, 'build', 'Mods', 'Plugins')
EXE_NAME = 'EDF6VehicleCrew安装器'
OUT = os.path.join(ROOT, 'release')
WORK = os.path.join(ROOT, 'build', 'pyinstaller')

README = """EDF6VehicleCrew {version}（空中支援 / 载具乘员插件）

前提：游戏目录里已经装好 EDFModLoader（winmm.dll + ModLoader.ini + Mods 文件夹）。

安装 / 更新：
  1. 先退出游戏。
  2. 双击「EDF6VehicleCrew安装器.exe」，输入 1 回车。
     自动找 Steam 里的游戏目录；找不到会让你粘贴游戏目录（有 EDF6.exe 的那个文件夹）。
     也可以把 exe 放进游戏目录里再运行。
  3. 看到「安装完成」后启动游戏。

  不需要装 Python。战机、直升机、潜水母舰和呼叫武器是安装器用你自己游戏里的
  Root.cpk 现场生成的（不修改 Root.cpk）；全部生成成功后才开始写文件。
  武器表只动本插件自己的行（已有的行原地更新，新的追加在末尾），别的 MOD 的行原样保留，
  武器表和武器说明要么全部写入、要么保持原样。
  已有的 Mods/Plugins/EDF6VehicleCrew.ini 不会被覆盖：你的设置保留，只补进新版本新增的设置。

卸载：
  运行安装器输入 2。
  选 1 = 连呼叫武器一起删：武器表里它们的行变成「已卸载」的占位行（效果同原版 KM6 轰炸机呼叫），
         行号不变，存档里装备着也不会崩溃，以后重新安装会用回这些行。
  选 2 = 只删插件：武器留着，会按原版轰炸机呼叫，不影响游玩。

杀毒软件可能误报 PyInstaller 打包的 exe，这是打包方式本身的常见误报。
"""


def cmake_version() -> str:
    """CMakeLists.txt project(... VERSION x.y.z ...): the one version (src/version.h.in)."""
    with open(os.path.join(ROOT, 'CMakeLists.txt'), encoding='utf-8') as f:
        m = re.search(r'project\(\s*EDF6VehicleCrew\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)', f.read())
    if not m:
        raise SystemExit('CMakeLists.txt: no project(EDF6VehicleCrew VERSION x.y.z)')
    return m.group(1)


def build_exe() -> str:
    seps = os.pathsep
    cmd = [sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean', '--onefile', '--console',
           '--name', EXE_NAME, '--distpath', os.path.join(WORK, 'dist'), '--workpath', os.path.join(WORK, 'work'),
           '--specpath', WORK]
    for p in (os.path.join(ROOT, 'tools'), os.path.join(ROOT, 'pylib')):
        cmd += ['--paths', p]
    for mod in ('call_weapons', 'make_jets', 'make_sub', 'make_katyusha', 'katyusha_model', 'graft_pure', 'calls'):
        cmd += ['--hidden-import', mod]
    for mod in ('numpy', 'PIL', 'matplotlib', 'pandas', 'tkinter'):  # dev-only tools import these
        cmd += ['--exclude-module', mod]
    for name in ('EDF6VehicleCrew.dll', 'EDF6VehicleCrew.ini'):
        cmd += ['--add-data', f'{os.path.join(PLUGINS, name)}{seps}plugin']
    cmd.append(os.path.join(ROOT, 'tools', 'installer.py'))
    subprocess.run(cmd, check=True)
    return os.path.join(WORK, 'dist', EXE_NAME + '.exe')


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--suffix', default='')
    ap.add_argument('--expect', default='')
    a = ap.parse_args(argv)
    version = cmake_version()
    if a.expect and a.expect.lstrip('v') != version:
        raise SystemExit(f'CMakeLists.txt says {version}, expected {a.expect}: bump project(VERSION) first')
    for name in ('EDF6VehicleCrew.dll', 'EDF6VehicleCrew.ini'):
        if not os.path.isfile(os.path.join(PLUGINS, name)):
            raise SystemExit(f'missing {name} in build/Mods/Plugins: run build.cmd first')
    exe = build_exe()
    os.makedirs(OUT, exist_ok=True)
    zpath = os.path.join(OUT, f'EDF6VehicleCrew-{version}{a.suffix}.zip')
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED) as z:
        z.write(exe, EXE_NAME + '.exe')
        z.writestr('说明.txt', README.format(version=version).replace('\n', '\r\n').encode('utf-8-sig'))
    shutil.copy2(exe, os.path.join(OUT, EXE_NAME + '.exe'))
    print(f'wrote {os.path.getsize(zpath)} bytes')
    print(zpath)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
