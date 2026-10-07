"""Builds the unzip-and-run release: release/EDF6VehicleCrew-<version>.zip holding
  EDF6VehicleCrew安装器.exe  (tools/installer.py frozen by PyInstaller; inside it every plugin of installer.PLUGINS,
                              EDF6VehicleCrew and EDF6AutoTurret, each its dll + ini, and the data builders,
                              autoturret/tools/build.py among them)
  说明.txt
  models/<name>/...           the user-supplied vehicle models (RELEASE_MODELS) found by pylib/obj_model.py model_dir()
                              ($EDF6VC_MODELS, models/ in the repository, the developer's folder); the installer reads
                              them from models/ next to itself, and skips what a missing one would make
The version is the one CMakeLists.txt project(VERSION) sets, the same the DLL reports (src/version.h.in).
Run build.cmd first: the exe bundles build/Mods/Plugins/<plugin>.dll and .ini (plugin_data()) as they are now.
Needs PyInstaller (python -m pip install pyinstaller).

Usage: python tools/build_release.py [--suffix=TEXT] [--expect VERSION]
  --suffix   appended to the file name's version (CI: --suffix=-build.<commit>; with = since it starts with -)
  --expect   fail unless CMakeLists.txt's version is this one (CI: the pushed tag vX.Y.Z)
Prints the zip's path last.
"""
from __future__ import annotations

import argparse
import json
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
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import installer  # noqa: E402  (PLUGINS: what the exe ships and installs)
WORK = os.path.join(ROOT, 'build', 'pyinstaller')
RELEASE_MODELS = ('twin_tank', 'drill_tank', 'sazabi')   # pylib/artillery_model.py MODEL, drill_model / sazabi_model.MODEL_SUBDIR

README = """EDF6VehicleCrew {version}（空中支援 / 载具乘员插件）

前提：游戏目录里已经装好 EDFModLoader（winmm.dll + ModLoader.ini + Mods 文件夹）。

包里有什么（安装器一次全装，不用再单独装别的）：
  - 插件 EDF6VehicleCrew.dll + EDF6VehicleCrew.ini：NPC 开载具 / 直升机、空中支援、战机、各种新载具；
  - 插件 EDF6AutoTurret.dll + EDF6AutoTurret.ini：防空车 / 玻尔斯 / 自行榴弹炮 / 喀秋莎的自瞄和近炸引信，
    泰坦和炮手座坦克的副炮自瞄，炮塔镜头下的锁定；
  - AutoTurret 的车辆数据：防空车（KG6 克卜勒系）改高射炮、玻尔斯对地、关卡里的 NPC 防空车、NPC 泰坦的副炮，
    以及这几辆车的武器说明行；
  - 呼叫武器、战机 / 直升机 / 无人机、潜水母舰、喀秋莎、自行榴弹炮、钻头战车、边三轮、降落伞、EMC 光束、
    大地图和测试场「大混战」关卡（都在你机器上现场生成，见下）。

安装 / 更新：
  1. 先退出游戏。
  2. 双击「EDF6VehicleCrew安装器.exe」，输入 1 回车。
     自动找 Steam 里的游戏目录；找不到会让你粘贴游戏目录（有 EDF6.exe 的那个文件夹）。
     也可以把 exe 放进游戏目录里再运行。
  3. 看到「安装完成」后启动游戏。

  不需要装 Python。战机、直升机、潜水母舰、呼叫武器和大地图是安装器用你自己游戏里的
  Root.cpk 和 Chunk02.cpk 现场生成的（不修改原始资源包）；全部生成成功后才开始写文件。
  更新时按资源组校验已有文件：生成器、原始资源包和外部模型没有变化，文件也完整时直接复用，
  不再全量重建。缺失或被修改的资源会重新生成；共享武器表仍会检查，以兼容其它 MOD。
  首次使用支持增量更新的安装器仍需生成一次，之后更新可复用。每一步会显示生成或复用及耗时。
  自行榴弹炮的外形来自压缩包里的 models 文件夹（双管坦克模型），请和 exe 放在一起解压；
  没有这个文件夹时自行榴弹炮用 Kepler 原版外形。
  安装器还会写入：
    - 大地图：测试场那张平原拼成 3 x 3 块（无缝），只影响测试场那一关；
    - 测试场关卡：没有敌人，只有靶子（地面和空中各一半，打掉了会重新出现）；地上停着本插件新加 / 改造的
      每种可驾驶载具（战斗机、攻击机、各种可开的飞机和空中航母、喀秋莎、自行榴弹炮、钻头战车、边三轮、
      直升机、坦克、高射炮车、深渊爬行者、EMC、尼克斯、普罗透斯），外加一架 NPC 驾驶的战斗机。

联机一起玩：
  房间里每个人都要装同一个版本（同一个安装包），地图、关卡和载具才一致。
  测试场关卡不限制兵种和武器，各自选自己的。
  武器表只动本插件自己的行（已有的行原地更新，新的追加在末尾），别的 MOD 的行原样保留，
  武器表和武器说明要么全部写入、要么保持原样。
  已有的 Mods/Plugins/EDF6VehicleCrew.ini 和 EDF6AutoTurret.ini 不会被覆盖：你的设置保留，只补进新版本新增的设置。
  AutoTurret 的车辆数据只替换它自己写过的文件；要替换别的 MOD 的文件时会先问你，同意后先备份，卸载时恢复。

卸载：
  运行安装器输入 2。
  选 1 = 连呼叫武器、生成的模型和 AutoTurret 的车辆数据一起删：武器表里呼叫武器的行变成「已卸载」的占位行
         （效果同原版 KM6 轰炸机呼叫），行号不变，存档里装备着也不会崩溃，以后重新安装会用回这些行；
         AutoTurret 改过的文件按备份恢复。
  选 2 = 只删两个插件：武器和车辆数据留着，会按原版轰炸机呼叫、防空车的炮照原版开火（没有自瞄），不影响游玩。

检查：
  运行安装器输入 5：逐项检查两个插件是不是本安装包的版本、ini 缺不缺新设置、AutoTurret 车辆数据、
  呼叫武器和生成的文件是否完整（只读，游戏开着也能查）。

测试（和开发者一起测）：
  测试站 https://edf6.fushi.moe （账号密码问开发者要）：下载测试版、看要测什么、提交反馈和录屏、看开发者回复。
  安装器输入 3 = 下载最新测试版；输入 4 = 回传日志（插件日志、设置、版本、最近的崩溃转储，游戏开着也能传）。

杀毒软件可能误报 PyInstaller 打包的 exe，这是打包方式本身的常见误报。
"""


def cmake_version() -> str:
    """CMakeLists.txt project(... VERSION x.y.z ...): the one version (src/version.h.in)."""
    with open(os.path.join(ROOT, 'CMakeLists.txt'), encoding='utf-8') as f:
        m = re.search(r'project\(\s*EDF6VehicleCrew\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)', f.read())
    if not m:
        raise SystemExit('CMakeLists.txt: no project(EDF6VehicleCrew VERSION x.y.z)')
    return m.group(1)


def plugin_data() -> list[str]:
    """The build/Mods/Plugins files the exe bundles: each plugin installer.PLUGINS installs, its dll and its ini."""
    return [name + ext for name, _ in installer.PLUGINS for ext in installer.PLUGIN_FILES]


def build_exe(name: str) -> str:
    """name: the zip's name without .zip, bundled as plugin/build_info.json (the installer shows it, and its
    menu 3 compares it with the test site's newest build)."""
    seps = os.pathsep
    os.makedirs(WORK, exist_ok=True)
    info = os.path.join(WORK, 'build_info.json')
    with open(info, 'w', encoding='utf-8') as f:
        json.dump({'name': name}, f)
    sys.path.insert(0, os.path.join(ROOT, 'pylib'))
    import buildcache
    recipes = os.path.join(WORK, buildcache.RECIPES)
    with open(recipes, 'w', encoding='utf-8') as f:
        json.dump(buildcache.source_recipes(ROOT), f, sort_keys=True)
    cmd = [sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean', '--onefile', '--console',
           '--name', EXE_NAME, '--distpath', os.path.join(WORK, 'dist'), '--workpath', os.path.join(WORK, 'work'),
           '--specpath', WORK]
    for p in (os.path.join(ROOT, 'tools'), os.path.join(ROOT, 'pylib'), os.path.join(ROOT, 'testrange'),
              os.path.join(ROOT, 'autoturret', 'tools')):
        cmd += ['--paths', p]
    for mod in ('call_weapons', 'make_jets', 'make_sub', 'make_katyusha', 'katyusha_model', 'make_artillery', 'artillery_model', 'ragdoll_fit', 'make_chute', 'chute_model', 'obj_model', 'texfile', 'make_drill', 'drill_model', 'make_stock_stores', 'graft_pure', 'primer_fighter_model', 'calls', 'make_sidecar', 'sidecar_model',
                'make_bigmap', 'bigmap', 'seams', 'fmb', 'hkcms', 'hktag', 'gen', 'rmpa', 'jet_models', 'jet_gear', 'weapons',
                'testhub', 'make_emc', 'centipede_model', 'dragonfly_model', 'buildcache', 'rootcpk', 'ledger',
                'cas_pose', 'aircraft_collision',
                'make_sazabi', 'sazabi_model', 'sazabi_arms', 'procmesh', 'make_edf5_campaign',
                'build'):   # every module installer.py imports in a function (selftest release_imports); build is
        # autoturret/tools/build.py (--paths above comes before site-packages, where pip's own `build` may be)
        cmd += ['--hidden-import', mod]
    for mod in ('matplotlib', 'pandas', 'tkinter'):  # Pillow builds procedural textures; numpy builds map seams
        cmd += ['--exclude-module', mod]
    for name in plugin_data():
        cmd += ['--add-data', f'{os.path.join(PLUGINS, name)}{seps}plugin']
    cmd += ['--add-data', f'{info}{seps}plugin']
    cmd += ['--add-data', f'{recipes}{seps}plugin']
    # the EDF5 campaign's titles and briefings (make_edf5_campaign.TEXT reads them from the bundle when frozen)
    cmd += ['--add-data', f'{os.path.join(ROOT, "edf5campaign", "missions.json")}{seps}edf5campaign']
    cmd.append(os.path.join(ROOT, 'tools', 'installer.py'))
    subprocess.run(cmd, check=True)
    return os.path.join(WORK, 'dist', EXE_NAME + '.exe')


def release_models() -> list[tuple[str, str]]:
    """(file on disk, path in the zip) of every RELEASE_MODELS folder found (each one missing is reported)."""
    sys.path.insert(0, os.path.join(ROOT, 'pylib'))
    import obj_model
    out = []
    for name in RELEASE_MODELS:
        folder = obj_model.model_dir(name)
        if folder is None:
            print(f'note: model {name} not found ({", ".join(obj_model.model_roots())}): the release goes without it')
            continue
        for f in sorted(os.listdir(folder)):
            if os.path.isfile(os.path.join(folder, f)):
                out.append((os.path.join(folder, f), f'models/{name}/{f}'))
    return out


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--suffix', default='')
    ap.add_argument('--expect', default='')
    a = ap.parse_args(argv)
    version = cmake_version()
    if a.expect and a.expect.lstrip('v') != version:
        raise SystemExit(f'CMakeLists.txt says {version}, expected {a.expect}: bump project(VERSION) first')
    for name in plugin_data():
        if not os.path.isfile(os.path.join(PLUGINS, name)):
            raise SystemExit(f'missing {name} in build/Mods/Plugins: run build.cmd first')
    exe = build_exe(f'EDF6VehicleCrew-{version}{a.suffix}')
    os.makedirs(OUT, exist_ok=True)
    zpath = os.path.join(OUT, f'EDF6VehicleCrew-{version}{a.suffix}.zip')
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED) as z:
        z.write(exe, EXE_NAME + '.exe')
        z.writestr('说明.txt', README.format(version=version).replace('\n', '\r\n').encode('utf-8-sig'))
        for src, arc in release_models():
            z.write(src, arc)
    shutil.copy2(exe, os.path.join(OUT, EXE_NAME + '.exe'))
    print(f'wrote {os.path.getsize(zpath)} bytes')
    print(zpath)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
